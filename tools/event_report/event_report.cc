// event_report 1 - Elite Dangerous' events from Windows' logs, for a player to send back: crashes and hangs of the game
// and its launcher (the Application log), graphics driver resets and errors, restarts without a clean shutdown and blue
// screens (the System log), Windows Error Reporting's saved reports of the game (the files loaded into it when it
// crashed) and the crash dump folders (names, sizes and dates only), over the last 30 days. Only the fields report.h
// keeps reach the text; user and computer names, e-mail addresses and SIDs are replaced, control characters escaped.
// The text goes to event_report-<date>-<time>.txt beside the exe (a file of its own for each run) and to the console.
// Nothing is sent anywhere; no administrator rights.
#include "fold.h"
#include "report.h"

#include <edplugin_common/file/file.h>
#include <edplugin_common/paths/paths.h>

#include <windows.h>
#include <winevt.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
  {
  using namespace event_report;

  ///\brief a line of progress, to the console only (ASCII, nothing from the logs)
  [[gnu::format(printf, 1, 2)]]
  auto progress(char const * format, ...) -> void
    {
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::fputs("\n", stdout);
    }

  [[nodiscard]]
  auto utf8(std::wstring_view text) -> std::string
    {
    if(text.empty())
      return {};
    int const n{WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr)};
    if(n <= 0)
      return "?";
    std::string out(std::size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
    return out;
    }

  [[nodiscard]]
  auto wide(std::string_view text) -> std::wstring
    {
    if(text.empty())
      return {};
    int const n{MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0)};
    if(n <= 0)
      return {};
    std::wstring out(std::size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
    return out;
    }

  [[nodiscard]]
  auto filetime(FILETIME const & f) noexcept -> std::uint64_t
    {
    return std::uint64_t(f.dwHighDateTime) << 32 | f.dwLowDateTime;
    }

  ///\brief a FILETIME (100 ns since 1601, UTC) as local time with its offset at that moment (the time zone's rules
  /// for that date: summer time or not)
  [[nodiscard]]
  auto local(std::uint64_t utc) -> local_time_t
    {
    FILETIME const f{DWORD(utc), DWORD(utc >> 32)};
    SYSTEMTIME u{}, l{};
    if(not FileTimeToSystemTime(&f, &u))
      return {};
    if(not SystemTimeToTzSpecificLocalTime(nullptr, &u, &l))
      l = u;
    // the offset from the two times as SYSTEMTIME gives them (whole milliseconds both)
    FILETIME lf{}, uf{};
    (void)SystemTimeToFileTime(&l, &lf);
    (void)SystemTimeToFileTime(&u, &uf);
    auto const offset{(static_cast<long long>(filetime(lf)) - static_cast<long long>(filetime(uf))) / 600000000LL};
    return {l.wYear, l.wMonth, l.wDay, l.wHour, l.wMinute, l.wSecond, int(offset)};
    }

  ///\brief an event log handle closed with the object
  struct evt_t
    {
    EVT_HANDLE h{};

    evt_t() = default;
    explicit evt_t(EVT_HANDLE handle) : h{handle} {}
    evt_t(evt_t const &) = delete;
    auto operator=(evt_t const &) -> evt_t & = delete;

    ~evt_t()
      {
      if(h)
        EvtClose(h);
      }
    };

  ///\brief an event's values in `context`, into `buffer` (grown up to 1 MB); false when they could not be rendered
  [[nodiscard]]
  auto render(EVT_HANDLE context, EVT_HANDLE event, std::vector<std::byte> & buffer, DWORD & count) -> bool
    {
    for(int tries{}; tries != 4; ++tries)
      {
      DWORD used{};
      count = 0;
      if(EvtRender(context, event, EvtRenderEventValues, DWORD(buffer.size()), buffer.data(), &used, &count))
        return true;
      if(GetLastError() != ERROR_INSUFFICIENT_BUFFER or used > (1u << 20))
        return false;
      buffer.resize(used);
      }
    return false;
    }

  ///\brief a value as text: strings as they are, numbers in decimal, the log's hexadecimal ones with "0x"; nothing for
  /// SIDs, binary data, times and arrays (none of them is kept)
  [[nodiscard]]
  auto text_of(EVT_VARIANT const & v) -> std::string
    {
    if(v.Type & EVT_VARIANT_TYPE_ARRAY)
      return {};
    char number[32]{};
    switch(v.Type & EVT_VARIANT_TYPE_MASK)
      {
      case EvtVarTypeString: return v.StringVal ? utf8(v.StringVal) : std::string{};
      case EvtVarTypeAnsiString: return v.AnsiStringVal ? std::string{v.AnsiStringVal} : std::string{};
      case EvtVarTypeSByte: std::snprintf(number, sizeof number, "%d", int(v.SByteVal)); break;
      case EvtVarTypeByte: std::snprintf(number, sizeof number, "%u", unsigned(v.ByteVal)); break;
      case EvtVarTypeInt16: std::snprintf(number, sizeof number, "%d", int(v.Int16Val)); break;
      case EvtVarTypeUInt16: std::snprintf(number, sizeof number, "%u", unsigned(v.UInt16Val)); break;
      case EvtVarTypeInt32: std::snprintf(number, sizeof number, "%ld", static_cast<long>(v.Int32Val)); break;
      case EvtVarTypeUInt32:
        std::snprintf(number, sizeof number, "%lu", static_cast<unsigned long>(v.UInt32Val));
        break;
      case EvtVarTypeInt64: std::snprintf(number, sizeof number, "%lld", static_cast<long long>(v.Int64Val)); break;
      case EvtVarTypeUInt64:
      case EvtVarTypeSizeT:
        std::snprintf(number, sizeof number, "%llu", static_cast<unsigned long long>(v.UInt64Val));
        break;
      case EvtVarTypeHexInt32:
        std::snprintf(number, sizeof number, "0x%08lx", static_cast<unsigned long>(v.UInt32Val));
        break;
      case EvtVarTypeHexInt64:
        std::snprintf(number, sizeof number, "0x%016llx", static_cast<unsigned long long>(v.UInt64Val));
        break;
      case EvtVarTypeBoolean: return v.BooleanVal ? "true" : "false";
      default: return {};
      }
    return number;
    }

  ///\brief the entries of `channel` that `xpath` selects, newest first, those report.h shows kept
  auto read_log(wchar_t const * channel, wchar_t const * xpath, char const * name, input_t & in) -> void
    {
    source_t source{name, 0, false, {}};
    evt_t const query{EvtQuery(nullptr, channel, xpath, EvtQueryChannelPath | EvtQueryReverseDirection)};
    evt_t const system{EvtCreateRenderContext(0, nullptr, EvtRenderContextSystem)};
    evt_t const user{EvtCreateRenderContext(0, nullptr, EvtRenderContextUser)};
    if(not query.h or not system.h or not user.h)
      {
      char why[64]{};
      std::snprintf(why, sizeof why, "could not be read (error %lu)", GetLastError());
      source.failure = why;
      progress("Reading %s... could not", name);
      in.sources.push_back(std::move(source));
      return;
      }
    std::vector<std::byte> buffer(64 * 1024);
    int kept{}, not_rendered{};
    for(;;)
      {
      EVT_HANDLE events[16]{};
      DWORD returned{};
      if(source.read >= scanned_most)
        {
        source.at_limit = true;
        break;
        }
      if(not EvtNext(query.h, DWORD(std::size(events)), events, 5000, 0, &returned))
        {
        DWORD const error{GetLastError()};
        if(error != ERROR_NO_MORE_ITEMS)
          {
          char why[64]{};
          std::snprintf(why, sizeof why, "stopped after %d entries (error %lu)", source.read, error);
          in.skipped.push_back(std::string{name} + ": " + why);
          }
        break;
        }
      for(DWORD i{}; i != returned; ++i)
        {
        evt_t const event{events[i]};
        ++source.read;
        DWORD count{};
        if(not render(system.h, event.h, buffer, count) or count <= EvtSystemTimeCreated)
          {
          ++not_rendered;
          continue;
          }
        auto const * const sv{reinterpret_cast<EVT_VARIANT const *>(buffer.data())};
        std::string const provider{text_of(sv[EvtSystemProviderName])};
        unsigned const id{(sv[EvtSystemEventID].Type & EVT_VARIANT_TYPE_MASK) == EvtVarTypeUInt16
                            ? unsigned(sv[EvtSystemEventID].UInt16Val)
                            : 0u};
        std::uint64_t const time{(sv[EvtSystemTimeCreated].Type & EVT_VARIANT_TYPE_MASK) == EvtVarTypeFileTime
                                   ? sv[EvtSystemTimeCreated].FileTimeVal
                                   : 0u};
        std::vector<std::string> values;
        if(render(user.h, event.h, buffer, count))
          {
          auto const * const uv{reinterpret_cast<EVT_VARIANT const *>(buffer.data())};
          for(DWORD k{}; k != std::min<DWORD>(count, 64); ++k)
            values.push_back(text_of(uv[k]));
          }
        std::optional<kind_e> const kind{kind_of(provider, id, values)};
        if(not kind)
          continue;
        in.records.push_back({*kind, local(time), provider, id, std::move(values)});
        ++kept;
        }
      }
    if(not_rendered)
      in.skipped.push_back(std::string{name} + ": " + std::to_string(not_rendered) + " entries could not be read");
    progress("Reading %s... %d entries read, %d about the game, its launcher or the graphics", name, source.read, kept);
    in.sources.push_back(std::move(source));
    }

  struct folder_t
    {
    std::wstring path;
    std::uint64_t time{};
    };

  ///\brief the subfolders of `dir` (no reparse point), newest first, at most `most`; false when `dir` could not be
  /// listed (`error` set; ERROR_PATH_NOT_FOUND / ERROR_FILE_NOT_FOUND for none)
  [[nodiscard]]
  auto list(std::wstring const & dir, bool folders, std::vector<folder_t> & found, DWORD & error) -> bool
    {
    WIN32_FIND_DATAW d{};
    HANDLE const h{FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &d,
                                    folders ? FindExSearchLimitToDirectories : FindExSearchNameMatch, nullptr, 0)};
    if(h == INVALID_HANDLE_VALUE)
      {
      error = GetLastError();
      return false;
      }
    do
      {
      bool const is_folder{(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0};
      std::wstring_view const name{d.cFileName};
      if((d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) or is_folder != folders or name == L"." or name == L"..")
        continue;
      found.push_back({d.cFileName, filetime(d.ftLastWriteTime)});
      }
    while(FindNextFileW(h, &d));
    FindClose(h);
    std::ranges::sort(found, [](folder_t const & a, folder_t const & b) { return a.time > b.time; });
    return true;
    }

  ///\brief a file's bytes, at most `most`; false when it is larger or could not be read
  [[nodiscard]]
  auto read_file(std::wstring const & path, std::size_t most, std::string & bytes, bool & too_large) -> bool
    {
    too_large = false;
    HANDLE const f{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if(f == INVALID_HANDLE_VALUE)
      return false;
    LARGE_INTEGER size{};
    bool ok{GetFileSizeEx(f, &size) != FALSE};
    too_large = ok and std::uint64_t(size.QuadPart) > most;
    ok = ok and not too_large;
    if(ok)
      {
      bytes.resize(std::size_t(size.QuadPart));
      DWORD got{};
      ok = bytes.empty() or (ReadFile(f, bytes.data(), DWORD(bytes.size()), &got, nullptr) and got == bytes.size());
      }
    CloseHandle(f);
    return ok;
    }

  ///\brief the saved reports in a store of Windows Error Reporting that are the game's, the launcher's or a graphics
  /// hang's (LiveKernelEvent)
  auto read_store(std::wstring const & dir, char const * name, input_t & in, std::map<std::string, int> & skipped)
    -> void
    {
    source_t source{name, 0, false, {}};
    std::vector<folder_t> folders;
    DWORD error{};
    if(not list(dir, true, folders, error))
      {
      if(error == ERROR_PATH_NOT_FOUND or error == ERROR_FILE_NOT_FOUND)
        source.name += " (no such folder)";
      else
        source.failure = "could not be read (error " + std::to_string(error) + ")";
      in.sources.push_back(std::move(source));
      return;
      }
    if(folders.size() > std::size_t(wer_folders_most))
      {
      folders.resize(std::size_t(wer_folders_most));
      source.at_limit = true;
      }
    int kept{};
    for(folder_t const & f: folders)
      {
      ++source.read;
      std::string bytes;
      bool too_large{};
      if(not read_file(dir + L"\\" + f.path + L"\\Report.wer", wer_bytes_most, bytes, too_large))
        {
        ++skipped[too_large ? "larger than 4 MB" : "not readable"];
        continue;
        }
      wer_t w;
      if(wer_error_e const e{parse_wer(bytes, w)}; e != wer_error_e::none)
        {
        ++skipped[std::string{wer_error_text(e)}];
        continue;
        }
      bool const game{equal_windows(w.app_name, "EliteDangerous64.exe") or equal_windows(w.app_name, "EDLaunch.exe")};
      if(not game and not equal_windows(w.event_type, "LiveKernelEvent"))
        continue;
      w.time = local(f.time);
      in.reports.push_back(std::move(w));
      ++kept;
      }
    progress("Reading %s... %d folders, %d about the game or the graphics", name, source.read, kept);
    in.sources.push_back(std::move(source));
    }

  ///\brief the dump files in `dir`; with `game_only` those named after the game or the launcher only
  auto read_dumps(std::wstring const & dir, char const * name, bool game_only, input_t & in) -> void
    {
    source_t source{name, 0, false, {}};
    std::vector<folder_t> files;
    DWORD error{};
    if(not list(dir, false, files, error))
      {
      if(error == ERROR_PATH_NOT_FOUND or error == ERROR_FILE_NOT_FOUND)
        source.name += " (no such folder)";
      else
        source.failure = "could not be read (error " + std::to_string(error) + ")";
      in.sources.push_back(std::move(source));
      return;
      }
    for(folder_t const & f: files)
      {
      std::string const file{utf8(f.path)};
      if(game_only and not file.starts_with("EliteDangerous") and not file.starts_with("EDLaunch"))
        continue;
      ++source.read;
      WIN32_FILE_ATTRIBUTE_DATA a{};
      std::uint64_t bytes{};
      if(GetFileAttributesExW((dir + L"\\" + f.path).c_str(), GetFileExInfoStandard, &a))
        bytes = std::uint64_t(a.nFileSizeHigh) << 32 | a.nFileSizeLow;
      in.dumps.push_back({file, bytes, local(f.time)});
      }
    progress("Reading %s... %d crash dumps", name, source.read);
    in.sources.push_back(std::move(source));
    }

  ///\brief the names looked for in the text, and a note for one too short to look for
  [[nodiscard]]
  auto tokens(input_t & in) -> std::vector<token_t>
    {
    std::vector<token_t> found;
    auto const add = [&](std::wstring const & name, std::string_view replacement, char const * what)
      {
      std::string const text{utf8(name)};
      if(text.empty())
        return;
      if(std::ranges::any_of(found, [&](token_t const & t) { return equal_windows(t.text, text); }))
        return;
      if(text.size() < token_least)
        in.notes.push_back(std::string{what} + " is too short to look for in the text: check the file for it yourself");
      found.push_back({text, replacement});
      };
    wchar_t user[257]{};
    DWORD n{DWORD(std::size(user))};
    if(GetUserNameW(user, &n))
      add(user, "<user>", "the user name");
    std::wstring const profile{edplugin_common::environment(L"USERPROFILE")};
    add(edplugin_common::file_name_of(profile), "<user>", "the profile folder's name");
    if(not profile.empty())
      {
      wchar_t shortened[MAX_PATH]{};
      DWORD const length{GetShortPathNameW(profile.c_str(), shortened, MAX_PATH)};
      if(length != 0 and length < MAX_PATH)
        add(edplugin_common::file_name_of(shortened), "<user>", "the profile folder's short name");
      }
    for(COMPUTER_NAME_FORMAT const format: {ComputerNameNetBIOS, ComputerNameDnsHostname})
      {
      wchar_t computer[256]{};
      DWORD size{DWORD(std::size(computer))};
      if(GetComputerNameExW(format, computer, &size))
        add(computer, "<computer>", "the computer name");
      }
    return found;
    }

  auto windows_version() -> std::string
    {
    using rtl_t = LONG(WINAPI *)(OSVERSIONINFOW *);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    HMODULE const ntdll{GetModuleHandleW(L"ntdll.dll")};
    auto const rtl{ntdll ? reinterpret_cast<rtl_t>(reinterpret_cast<void *>(GetProcAddress(ntdll, "RtlGetVersion")))
                         : nullptr};
    if(not rtl or rtl(&v) != 0)
      return "?";
    return std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + "."
         + std::to_string(v.dwBuildNumber);
    }

  ///\brief the text for the console: bytes outside ASCII as '?', one for each character
  [[nodiscard]]
  auto ascii(std::string_view text) -> std::string
    {
    std::string out;
    for(char const c: text)
      {
      auto const b{static_cast<unsigned char>(c)};
      if(b < 0x80)
        out += c;
      else if((b & 0xC0) != 0x80)
        out += '?';
      }
    return out;
    }

  constexpr wchar_t application_query[]{
    L"*[System[((Provider[@Name='Application Error'] and EventID=1000) or (Provider[@Name='Application Hang'] and "
    L"EventID=1002) or (Provider[@Name='Windows Error Reporting'] and EventID=1001) or (Provider[@Name='.NET Runtime'] "
    L"and EventID=1026)) and TimeCreated[timediff(@SystemTime) <= 2592000000]]]"};
  // the providers wanted in the System log are filtered after reading (Intel's by a prefix, which the query cannot
  // say): its errors and warnings of the period are read
  constexpr wchar_t system_query[]{
    L"*[System[(Level=1 or Level=2 or Level=3) and TimeCreated[timediff(@SystemTime) <= 2592000000]]]"};
  static_assert(30ull * 24 * 60 * 60 * 1000 == 2592000000ull and days == 30, "the queries' period is days");
  }  // namespace

auto main() -> int
  {
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
  FILETIME began{};
  GetSystemTimeAsFileTime(&began);
  input_t in;
  in.now = local(filetime(began));
  std::string text;
  try
    {
    progress("event_report 1: reading Windows' logs about Elite Dangerous (the last %d days)", days);
    in.windows_version = windows_version();
    wchar_t windows[MAX_PATH]{};
    UINT const length{GetSystemWindowsDirectoryW(windows, MAX_PATH)};
    if(length != 0 and length < MAX_PATH)
      in.windows_dir = utf8(std::wstring_view{windows, length}) + (windows[length - 1] == L'\\' ? "" : "\\");
    read_log(L"Application", application_query, "the Application log", in);
    read_log(L"System", system_query, "the System log", in);
    std::map<std::string, int> skipped;
    std::wstring const machine{edplugin_common::environment(L"ProgramData")};
    std::wstring const user{edplugin_common::environment(L"LOCALAPPDATA")};
    for(auto const & [root, place]: {std::pair{machine, "the machine's"}, std::pair{user, "the user's"}})
      for(wchar_t const * store: {L"ReportArchive", L"ReportQueue"})
        {
        std::string const name{std::string{"Windows Error Reporting: "} + place + " " + utf8(store)};
        if(root.empty())
          in.sources.push_back({name, 0, false, "its folder not known"});
        else
          read_store(root + L"\\Microsoft\\Windows\\WER\\" + store, name.c_str(), in, skipped);
        }
    for(auto const & [why, count]: skipped)
      in.skipped.push_back(std::to_string(count) + " saved report(s) not read: " + why);
    std::wstring const temp{edplugin_common::environment(L"TEMP")};
    if(not temp.empty())
      read_dumps(temp + L"\\Frontier Developments\\EliteDangerous\\CrashDumps", "the game's crash dump folder", false,
                 in);
    if(not user.empty())
      read_dumps(user + L"\\CrashDumps", "Windows' crash dump folder", true, in);
    std::vector<token_t> const names{tokens(in)};
    text = report(in, names, equal_windows);
    }
  catch(...)
    {
    text += "\nstopped: an exception (out of memory or the like); the lines above are what was found\n";
    }
  std::printf("\n%s\n", ascii(text).c_str());

  // beside the exe, under a name no file has yet: an earlier run's result is never replaced
  std::string file;
  for(char const c: text)
    {
    if(c == '\n')
      file += '\r';
    file += c;
    }
  std::wstring const dir{edplugin_common::directory_of(edplugin_common::module_path(nullptr))};
  std::string name;
  std::wstring path;
  for(int n{1}; not dir.empty() and n != 10; ++n)
    {
    name = result_name(in.now, n);
    path = dir + L"\\" + wide(name);
    if(GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES and GetLastError() == ERROR_FILE_NOT_FOUND)
      break;
    path.clear();
    }
  if(not path.empty() and edplugin_common::write_whole_file(path, file.data(), file.size()))
    std::printf("written: %s beside the program - read it before you send it\n", name.c_str());
  else
    std::printf("could not write %s beside the program; the text above is the result\n",
                name.empty() ? "the result file" : name.c_str());
  std::printf("press Enter to close\n");
  (void)std::getchar();
  return 0;
  }
