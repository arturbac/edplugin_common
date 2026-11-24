// event_report - the part without a Windows header (report.h).
#include "report.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>

namespace event_report
  {
  namespace
    {
    constexpr std::string_view game_exe{"EliteDangerous64.exe"};
    constexpr std::string_view launcher_exe{"EDLaunch.exe"};

    [[nodiscard]]
    constexpr auto lower(char c) noexcept -> char
      {
      return c >= 'A' and c <= 'Z' ? char(c - 'A' + 'a') : c;
      }

    [[nodiscard]]
    auto starts_with_fold(std::string_view text, std::string_view start) noexcept -> bool
      {
      return text.size() >= start.size() and equal_ascii_fold(text.substr(0, start.size()), start);
      }

    [[nodiscard]]
    auto find_fold(std::string_view text, std::string_view part) noexcept -> std::size_t
      {
      for(std::size_t i{}; i + part.size() <= text.size(); ++i)
        if(equal_ascii_fold(text.substr(i, part.size()), part))
          return i;
      return std::string_view::npos;
      }

    ///\brief a byte of a word: letters, digits, '_' and every byte of a character outside ASCII
    [[nodiscard]]
    constexpr auto word_byte(char c) noexcept -> bool
      {
      auto const u{static_cast<unsigned char>(c)};
      return (u >= '0' and u <= '9') or (u >= 'a' and u <= 'z') or (u >= 'A' and u <= 'Z') or u == '_' or u >= 0x80;
      }

    auto append_utf8(std::string & out, std::uint32_t c) -> void
      {
      if(c < 0x80)
        out += char(c);
      else if(c < 0x800)
        {
        out += char(0xC0 | (c >> 6));
        out += char(0x80 | (c & 0x3F));
        }
      else if(c < 0x10000)
        {
        out += char(0xE0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
        }
      else
        {
        out += char(0xF0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3F));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
        }
      }

    ///\brief `text` cut to value_most bytes at a character's start, "..." added when cut; its first line only
    [[nodiscard]]
    auto cut(std::string_view text) -> std::string
      {
      std::size_t end{std::min(text.find_first_of("\r\n"), text.size())};
      bool cut_short{end != text.size() and text.find_first_not_of(" \t\r\n", end) != std::string_view::npos};
      if(end > value_most)
        {
        end = value_most;
        while(end != 0 and (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
          --end;
        cut_short = true;
        }
      std::string out{text.substr(0, end)};
      while(not out.empty() and (out.back() == ' ' or out.back() == '\t'))
        out.pop_back();
      if(cut_short)
        out += " ...";
      return out;
      }

    [[nodiscard]]
    auto value(record_t const & r, std::size_t i) -> std::string
      {
      return i < r.values.size() ? cut(r.values[i]) : std::string{};
      }

    ///\brief days from 1970-01-01 to a date of the proleptic Gregorian calendar
    [[nodiscard]]
    constexpr auto days_from_civil(int y, int m, int d) noexcept -> std::int64_t
      {
      y -= m <= 2 ? 1 : 0;
      int const era{(y >= 0 ? y : y - 399) / 400};
      int const yoe{y - era * 400};
      int const doy{(153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1};
      int const doe{yoe * 365 + yoe / 4 - yoe / 100 + doy};
      return std::int64_t(era) * 146097 + doe - 719468;
      }

    static_assert(days_from_civil(1970, 1, 1) == 0 and days_from_civil(2000, 3, 1) == 11017
                  and days_from_civil(2026, 11, 24) == 20781);

    [[nodiscard]]
    constexpr auto utc_seconds(local_time_t const & t) noexcept -> std::int64_t
      {
      return days_from_civil(t.year, t.month, t.day) * 86400 + t.hour * 3600 + t.minute * 60 + t.second
           - std::int64_t(t.offset_minutes) * 60;
      }

    [[nodiscard]]
    auto format(char const * f, auto... args) -> std::string
      {
      int const n{std::snprintf(nullptr, 0, f, args...)};
      if(n <= 0)
        return {};
      std::string out(std::size_t(n) + 1, '\0');
      std::snprintf(out.data(), out.size(), f, args...);
      out.pop_back();
      return out;
      }

    enum struct code_e
      {
      exception,      ///< an exception code, hexadecimal with or without "0x"
      stop_code,      ///< a stop code, decimal or hexadecimal with "0x" (number_value)
      stop_code_hex,  ///< a stop code, hexadecimal with or without "0x" (LiveKernelEvent's "141")
      };

    ///\brief "0xc0000005 (access violation ...)" for a code as the log gave it; the text itself when it is no number
    [[nodiscard]]
    auto code_text(std::string_view text, code_e kind) -> std::string
      {
      std::optional<std::uint64_t> const code{kind == code_e::stop_code ? number_value(text) : hex_value(text)};
      if(not code)
        return cut(text);
      std::string out{format("0x%llx", static_cast<unsigned long long>(*code))};
      std::string_view const words{kind == code_e::exception ? exception_text(*code) : bugcheck_text(*code)};
      if(not words.empty())
        (out += " (") += std::string{words} + ")";
      return out;
      }

    ///\brief names joined by ", " in lines of at most 116 bytes, each but the first starting with `indent`
    auto add_list(std::string & out, std::string_view label, std::vector<std::string> const & names,
                  std::string_view indent) -> void
      {
      std::string line{std::string{label}};
      bool first{true};
      for(std::string const & name: names)
        {
        std::string const part{first ? name : ", " + name};
        if(not first and line.size() + part.size() > 116)
          {
          out += line + ",\n";
          line = std::string{indent} + name;
          }
        else
          line += part;
        first = false;
        }
      out += line + "\n";
      }

    ///\brief the files a saved report holds, by where they lie; nothing for one that holds none (a LiveKernelEvent's)
    auto add_modules(std::string & out, wer_t const & w, std::string_view windows_dir) -> void
      {
      if(w.modules.empty())
        return;
      std::string game_dir{w.app_path.substr(0, w.app_path.find_last_of("\\/") + 1)};
      if(w.app_path.find_first_of("\\/") == std::string::npos)
        game_dir.clear();
      std::vector<std::string> drivers, game, elsewhere;
      int in_windows{};
      for(std::string const & path: w.modules)
        {
        std::string name{file_name_of(path)};
        switch(place_of(path, game_dir, windows_dir))
          {
          case place_e::graphics_driver: drivers.push_back(std::move(name)); break;
          case place_e::game_folder: game.push_back(std::move(name)); break;
          case place_e::windows: ++in_windows; break;
          case place_e::elsewhere: elsewhere.push_back(std::move(name)); break;
          }
        }
      out += format("     files loaded into it (Windows' saved report, %zu%s):\n", w.modules.size(),
                    w.modules_cut ? ", the list cut" : "");
      add_list(out, "       graphics drivers:   ", drivers.empty() ? std::vector<std::string>{"none"} : drivers,
               "                           ");
      add_list(out, "       the game's folder:  ", game.empty() ? std::vector<std::string>{"none"} : game,
               "                           ");
      add_list(out, "       from elsewhere:     ", elsewhere.empty() ? std::vector<std::string>{"none"} : elsewhere,
               "                           ");
      out += format("       and %d from the Windows folder\n", in_windows);
      }

    [[nodiscard]]
    auto of_kind(input_t const & in, std::initializer_list<kind_e> kinds) -> std::vector<record_t const *>
      {
      std::vector<record_t const *> found;
      for(record_t const & r: in.records)
        if(std::ranges::find(kinds, r.kind) != kinds.end())
          found.push_back(&r);
      std::ranges::stable_sort(found, [](record_t const * a, record_t const * b) { return later(a->time, b->time); });
      return found;
      }

    ///\brief a section's title, "(newest 20 of 31 shown)" when more
    auto add_title(std::string & out, std::string_view title, std::size_t count) -> void
      {
      out += "\n" + std::string{title};
      if(count > shown)
        out += format(" (newest %zu of %zu shown)", shown, count);
      out += "\n";
      if(count == 0)
        out += format("  none in the last %d days\n", days);
      }

    [[nodiscard]]
    auto line_start(record_t const & r) -> std::string
      {
      return "  " + time_text(r.time) + "  ";
      }

    [[nodiscard]]
    auto find_report(input_t const & in, std::string_view id) -> wer_t const *
      {
      if(id.empty())
        return nullptr;
      for(wer_t const & w: in.reports)
        if(wer_has_id(w, id))
          return &w;
      return nullptr;
      }

    ///\brief the exception's type from a .NET Runtime entry's text: "System.IO.IOException" from "Exception Info:
    /// System.IO.IOException" (.NET Framework) or "Exception Info: System.IO.IOException: <message>" (.NET), never the
    /// message (it may hold a path)
    [[nodiscard]]
    auto dotnet_exception(std::string_view text) -> std::string
      {
      std::size_t const at{find_fold(text, "Exception Info:")};
      if(at == std::string_view::npos)
        return "(its type not found in the entry)";
      std::string_view rest{text.substr(at + 15)};
      rest.remove_prefix(std::min(rest.find_first_not_of(" \t"), rest.size()));
      std::string_view const type{rest.substr(0, std::min(rest.find_first_of(": \t\r\n"), rest.size()))};
      return type.empty() ? std::string{"(its type not found in the entry)"} : cut(type);
      }
    }  // namespace

  auto time_text(local_time_t const & t) -> std::string
    {
    return format("%04d-%02d-%02d %02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
    }

  auto later(local_time_t const & a, local_time_t const & b) noexcept -> bool
    {
    return utc_seconds(a) > utc_seconds(b);
    }

  auto offset_text(int minutes) -> std::string
    {
    int const a{minutes < 0 ? -minutes : minutes};
    return format("%c%02d:%02d", minutes < 0 ? '-' : '+', a / 60, a % 60);
    }

  auto utf16le_to_utf8(std::string_view bytes) -> std::string
    {
    std::string out;
    out.reserve(bytes.size() / 2);
    std::size_t const units{bytes.size() / 2};
    auto const unit = [&](std::size_t i) -> std::uint32_t
      {
      return std::uint32_t(static_cast<unsigned char>(bytes[2 * i]))
           | std::uint32_t(static_cast<unsigned char>(bytes[2 * i + 1])) << 8;
      };
    for(std::size_t i{}; i != units; ++i)
      {
      std::uint32_t const c{unit(i)};
      if(c >= 0xD800 and c <= 0xDBFF and i + 1 != units and unit(i + 1) >= 0xDC00 and unit(i + 1) <= 0xDFFF)
        {
        append_utf8(out, 0x10000 + ((c - 0xD800) << 10) + (unit(i + 1) - 0xDC00));
        ++i;
        }
      else if(c >= 0xD800 and c <= 0xDFFF)
        append_utf8(out, 0xFFFD);
      else
        append_utf8(out, c);
      }
    return out;
    }

  auto equal_ascii_fold(std::string_view a, std::string_view b) noexcept -> bool
    {
    return a.size() == b.size() and std::ranges::equal(a, b, [](char x, char y) { return lower(x) == lower(y); });
    }

  auto file_name_of(std::string_view path) noexcept -> std::string_view
    {
    std::size_t const at{path.find_last_of("\\/")};
    return at == std::string_view::npos ? path : path.substr(at + 1);
    }

  auto hex_value(std::string_view text) noexcept -> std::optional<std::uint64_t>
    {
    if(text.size() > 2 and text[0] == '0' and lower(text[1]) == 'x')
      text.remove_prefix(2);
    if(text.empty() or text.size() > 16)
      return std::nullopt;
    std::uint64_t v{};
    for(char const c: text)
      {
      char const l{lower(c)};
      int const d{l >= '0' and l <= '9' ? l - '0' : l >= 'a' and l <= 'f' ? l - 'a' + 10 : -1};
      if(d < 0)
        return std::nullopt;
      v = v << 4 | std::uint64_t(d);
      }
    return v;
    }

  auto number_value(std::string_view text) noexcept -> std::optional<std::uint64_t>
    {
    if(text.size() > 2 and text[0] == '0' and lower(text[1]) == 'x')
      return hex_value(text);
    if(text.empty() or text.size() > 19)
      return std::nullopt;
    std::uint64_t v{};
    for(char const c: text)
      {
      if(c < '0' or c > '9')
        return std::nullopt;
      v = v * 10 + std::uint64_t(c - '0');
      }
    return v;
    }

  auto exception_text(std::uint64_t code) noexcept -> std::string_view
    {
    switch(code)
      {
      case 0xC0000005: return "access violation: a read or write of memory not allowed";
      case 0xC0000409: return "the program stopped itself on a broken check (stack buffer overrun or fail fast)";
      case 0xC0000374: return "heap corruption";
      case 0xC00000FD: return "stack overflow";
      case 0xC000001D: return "illegal instruction: the processor lacks one the program uses, such as AVX";
      case 0xC0000417: return "an invalid parameter to a C runtime function";
      case 0xC0000420: return "an assertion failed";
      case 0xC0000602: return "fail fast";
      case 0x40000015: return "the program ended itself (abort)";
      case 0x80000003: return "a breakpoint";
      case 0xE0434352: return "a .NET exception";
      default: return {};
      }
    }

  auto bugcheck_text(std::uint64_t code) noexcept -> std::string_view
    {
    switch(code)
      {
      case 0x0: return "no stop code: power lost, a hard hang or a forced power off";
      case 0x1E: return "KMODE_EXCEPTION_NOT_HANDLED";
      case 0x3B: return "SYSTEM_SERVICE_EXCEPTION";
      case 0x50: return "PAGE_FAULT_IN_NONPAGED_AREA";
      case 0x7E: return "SYSTEM_THREAD_EXCEPTION_NOT_HANDLED";
      case 0x9F: return "DRIVER_POWER_STATE_FAILURE";
      case 0xA: return "IRQL_NOT_LESS_OR_EQUAL";
      case 0xD1: return "DRIVER_IRQL_NOT_LESS_OR_EQUAL";
      case 0xEF: return "CRITICAL_PROCESS_DIED";
      case 0x116: return "VIDEO_TDR_FAILURE: the graphics driver did not recover";
      case 0x117: return "VIDEO_TDR_TIMEOUT_DETECTED: the graphics driver stopped responding and recovered";
      case 0x124: return "WHEA_UNCORRECTABLE_ERROR: a hardware error";
      case 0x133: return "DPC_WATCHDOG_VIOLATION";
      case 0x141: return "VIDEO_ENGINE_TIMEOUT_DETECTED: a graphics engine stopped responding";
      case 0x193: return "VIDEO_DXGKRNL_LIVEDUMP: Windows saved the graphics kernel's state";
      default: return {};
      }
    }

  auto kind_of(std::string_view provider, unsigned id, std::span<std::string const> values) -> std::optional<kind_e>
    {
    auto const at = [&](std::size_t i) { return i < values.size() ? std::string_view{values[i]} : std::string_view{}; };
    auto const program = [](std::string_view name) -> int
      { return equal_ascii_fold(name, game_exe) ? 1 : equal_ascii_fold(name, launcher_exe) ? 2 : 0; };
    if(provider == "Application Error" and id == 1000)
      {
      int const p{program(at(0))};
      return p == 1 ? std::optional{kind_e::game_crash} : p == 2 ? std::optional{kind_e::launcher_crash} : std::nullopt;
      }
    if(provider == "Application Hang" and id == 1002)
      return program(at(0)) == 1 ? std::optional{kind_e::game_hang} : std::nullopt;
    if(provider == ".NET Runtime" and id == 1026)
      return find_fold(at(0), "Application: EDLaunch.exe") != std::string_view::npos
             ? std::optional{kind_e::launcher_crash}
             : std::nullopt;
    if(provider == "Windows Error Reporting" and id == 1001)
      {
      std::string_view const event{at(2)};
      if(equal_ascii_fold(event, "LiveKernelEvent"))
        return kind_e::wer_kernel;
      if(program(at(5)) != 0 and not equal_ascii_fold(event, "APPCRASH") and not starts_with_fold(event, "AppHang"))
        return kind_e::wer_game;
      return std::nullopt;
      }
    if(provider == "Display" and id == 4101)
      return kind_e::display_reset;
    if(equal_ascii_fold(provider, "nvlddmkm") or equal_ascii_fold(provider, "amdkmdag") or intel_graphics(provider))
      return kind_e::graphics_driver;
    if(provider == "Microsoft-Windows-Kernel-Power" and id == 41)
      return kind_e::restart;
    if(provider == "Microsoft-Windows-WER-SystemErrorReporting" and id == 1001)
      return kind_e::blue_screen;
    if(provider == "EventLog" and id == 6008)
      return kind_e::unexpected_shutdown;
    return std::nullopt;
    }

  auto parse_wer(std::string_view bytes, wer_t & out) -> wer_error_e
    {
    if(bytes.size() > wer_bytes_most)
      return wer_error_e::too_large;
    if(bytes.size() < 2 or bytes.size() % 2 != 0 or static_cast<unsigned char>(bytes[0]) != 0xFF
       or static_cast<unsigned char>(bytes[1]) != 0xFE)
      return wer_error_e::not_utf16;
    std::string const text{utf16le_to_utf8(bytes.substr(2))};
    std::map<int, std::pair<std::string, std::string>> signature;
    std::map<int, std::string> modules;
    auto const index = [](std::string_view key, std::string_view name) -> int
      {
      // "name[n]..." -> n (0..9999), -1 otherwise
      if(not key.starts_with(name) or key.size() < name.size() + 3 or key[name.size()] != '[')
        return -1;
      std::size_t const close{key.find(']', name.size())};
      std::optional<std::uint64_t> const n{close == std::string_view::npos
                                             ? std::nullopt
                                             : number_value(key.substr(name.size() + 1, close - name.size() - 1))};
      return n and *n < 10000 ? int(*n) : -1;
      };
    std::size_t at{};
    while(at < text.size())
      {
      std::size_t end{text.find('\n', at)};
      if(end == std::string::npos)
        end = text.size();
      std::string_view line{std::string_view{text}.substr(at, end - at)};
      at = end + 1;
      if(line.ends_with('\r'))
        line.remove_suffix(1);
      std::size_t const eq{line.find('=')};
      if(eq == std::string_view::npos)
        continue;
      std::string_view const key{line.substr(0, eq)};
      std::string_view const val{line.substr(eq + 1)};
      if(key == "EventType")
        out.event_type = val;
      else if(key == "ReportIdentifier")
        out.report_id = val;
      else if(key == "IntegratorReportIdentifier")
        out.integrator_report_id = val;
      else if(key == "EventTime")
        out.event_time = number_value(val).value_or(0);
      else if(key == "AppName")
        out.app_name = val;
      else if(key == "AppPath")
        out.app_path = val;
      else if(int const s{index(key, "Sig")}; s >= 0)
        {
        if(key.ends_with("].Name"))
          signature[s].first = val;
        else if(key.ends_with("].Value"))
          signature[s].second = val;
        }
      else if(int const m{index(key, "LoadedModule")}; m >= 0)
        {
        if(modules.size() < wer_modules_most)
          modules[m] = val;
        else
          out.modules_cut = true;
        }
      }
    if(out.event_type.empty())
      return wer_error_e::no_event_type;
    for(auto & [n, nv]: signature)
      out.signature.push_back(std::move(nv));
    for(auto & [n, path]: modules)
      out.modules.push_back(std::move(path));
    return wer_error_e::none;
    }

  auto wer_error_text(wer_error_e e) noexcept -> std::string_view
    {
    switch(e)
      {
      case wer_error_e::none: return "read";
      case wer_error_e::too_large: return "larger than 4 MB";
      case wer_error_e::not_utf16: return "not UTF-16";
      case wer_error_e::no_event_type: return "no EventType";
      }
    return "?";
    }

  auto wer_folder_wanted(std::string_view folder) noexcept -> bool
    {
    return starts_with_fold(folder, "AppCrash_") or starts_with_fold(folder, "AppHang_")
        or starts_with_fold(folder, "Kernel_");
    }

  auto wer_program(wer_t const & w) noexcept -> std::string_view
    {
    return w.app_path.empty() ? std::string_view{w.app_name} : file_name_of(w.app_path);
    }

  auto wer_wanted(wer_t const & w, fold_equal_t equal) -> bool
    {
    if(equal_ascii_fold(w.event_type, "LiveKernelEvent"))
      return true;
    std::string_view const sig0{w.signature.empty() ? std::string_view{} : std::string_view{w.signature[0].second}};
    for(std::string_view const name: {file_name_of(w.app_path), std::string_view{w.app_name}, sig0})
      if(not name.empty() and (equal(name, game_exe) or equal(name, launcher_exe)))
        return true;
    return false;
    }

  auto wer_has_id(wer_t const & w, std::string_view id) noexcept -> bool
    {
    return not id.empty() and (equal_ascii_fold(w.report_id, id) or equal_ascii_fold(w.integrator_report_id, id));
    }

  auto dump_of_game(std::string_view name) noexcept -> bool
    {
    return starts_with_fold(name, "EliteDangerous") or starts_with_fold(name, "EDLaunch");
    }

  auto intel_graphics(std::string_view provider) noexcept -> bool
    {
    return starts_with_fold(provider, "igfx");
    }

  auto place_of(std::string_view path, std::string_view game_dir, std::string_view windows_dir) noexcept -> place_e
    {
    if(find_fold(path, "\\DriverStore\\") != std::string_view::npos)
      return place_e::graphics_driver;
    if(not game_dir.empty() and starts_with_fold(path, game_dir))
      return place_e::game_folder;
    if(not windows_dir.empty() and starts_with_fold(path, windows_dir))
      return place_e::windows;
    return place_e::elsewhere;
    }

  auto clean(std::string_view text, std::span<token_t const> tokens, fold_equal_t equal) -> cleaned_t
    {
    cleaned_t out;
    // the names, at word boundaries
    std::string named;
    for(std::size_t i{}; i < text.size();)
      {
      bool matched{};
      if(i == 0 or not word_byte(text[i - 1]))
        for(token_t const & t: tokens)
          {
          std::size_t const n{t.text.size()};
          if(n < token_least or i + n > text.size() or (i + n != text.size() and word_byte(text[i + n]))
             or not equal(text.substr(i, n), t.text))
            continue;
          named += t.replacement;
          ++out.replaced;
          i += n;
          matched = true;
          break;
          }
      if(not matched)
        named += text[i++];
      }
    // SIDs, e-mail addresses, OneDrive's organisation
    std::string & r{out.text};
    r.reserve(named.size());
    std::string_view const s{named};
    auto const local_byte = [](char c)
      { return word_byte(c) or c == '.' or c == '%' or c == '+' or c == '-'; };
    for(std::size_t i{}; i < s.size();)
      {
      if((s.substr(i).starts_with("S-1-5-21-") or s.substr(i).starts_with("S-1-12-1-"))
         and (i == 0 or not word_byte(s[i - 1])))
        {
        std::size_t end{i + 9};
        while(end < s.size() and ((s[end] >= '0' and s[end] <= '9') or s[end] == '-'))
          ++end;
        r += "<sid>";
        ++out.replaced;
        i = end;
        continue;
        }
      if(s.substr(i).starts_with("OneDrive - "))
        {
        std::size_t end{i + 11};
        while(end < s.size() and s[end] != '\\' and s[end] != '/' and s[end] != '\n' and s[end] != '\r')
          ++end;
        r += "OneDrive";
        ++out.replaced;
        i = end;
        continue;
        }
      if(s[i] == '@' and not r.empty() and local_byte(r.back()))
        {
        // the domain: word bytes and dots, with a dot inside
        std::size_t end{i + 1};
        bool dot{};
        for(; end < s.size() and (word_byte(s[end]) or s[end] == '.' or s[end] == '-'); ++end)
          dot = dot or s[end] == '.';
        while(end > i + 1 and s[end - 1] == '.')
          --end;
        if(dot and end > i + 1)
          {
          while(not r.empty() and local_byte(r.back()))
            r.pop_back();
          r += "<e-mail>";
          ++out.replaced;
          i = end;
          continue;
          }
        }
      r += s[i++];
      }
    return out;
    }

  auto escape_controls(std::string_view text) -> std::string
    {
    std::string out;
    out.reserve(text.size());
    for(std::size_t i{}; i < text.size();)
      {
      auto const b{static_cast<unsigned char>(text[i])};
      // one character's bytes and its code point; a byte that starts none is escaped alone
      std::size_t n{b < 0x80 ? 1u : (b & 0xE0) == 0xC0 ? 2u : (b & 0xF0) == 0xE0 ? 3u : (b & 0xF8) == 0xF0 ? 4u : 0u};
      std::uint32_t c{n == 1 ? b : n == 2 ? b & 0x1Fu : n == 3 ? b & 0x0Fu : b & 0x07u};
      for(std::size_t k{1}; n != 0 and k != n; ++k)
        if(i + k >= text.size() or (static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
          n = 0;
        else
          c = c << 6 | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
      bool const overlong{(n == 2 and c < 0x80) or (n == 3 and c < 0x800) or (n == 4 and c < 0x10000)};
      if(n == 0 or overlong or c > 0x10FFFF or (c >= 0xD800 and c <= 0xDFFF))
        {
        out += format("\\x%02x", unsigned(b));
        ++i;
        continue;
        }
      bool const control{(c < 0x20 and c != '\t' and c != '\n' and c != '\r') or c == 0x7F or (c >= 0x80 and c < 0xA0)};
      bool const direction{c == 0x061C or c == 0x200E or c == 0x200F or (c >= 0x202A and c <= 0x202E)
                           or (c >= 0x2066 and c <= 0x2069)};
      if(control or direction)
        out += format("\\u%04x", unsigned(c));
      else
        out.append(text.substr(i, n));
      i += n;
      }
    return out;
    }

  auto body(input_t const & in) -> std::string
    {
    std::string out;
    auto const crashes{of_kind(in, {kind_e::game_crash})};
    auto const hangs{of_kind(in, {kind_e::game_hang})};
    auto const launcher{of_kind(in, {kind_e::launcher_crash})};
    auto const restarts{of_kind(in, {kind_e::restart, kind_e::blue_screen, kind_e::unexpected_shutdown})};
    auto const graphics{of_kind(in, {kind_e::display_reset, kind_e::graphics_driver, kind_e::wer_kernel})};
    auto const other{of_kind(in, {kind_e::wer_game})};
    auto const count = [&](kind_e k)
      { return std::ranges::count_if(in.records, [k](record_t const & r) { return r.kind == k; }); };

    out += "SUMMARY\n";
    out += format("  game crashes: %zu", crashes.size());
    if(not crashes.empty())
      out += " (last " + time_text(crashes.front()->time) + ")";
    out += format("   game hangs: %zu   launcher crashes: %zu\n", hangs.size(), launcher.size());
    out += format("  restarts without a clean shutdown: %td   blue screens: %td   unexpected shutdowns: %td\n",
                  count(kind_e::restart), count(kind_e::blue_screen), count(kind_e::unexpected_shutdown));
    out += format("  graphics driver resets: %td   graphics driver errors: %td   graphics hangs Windows reported: "
                  "%td\n",
                  count(kind_e::display_reset), count(kind_e::graphics_driver), count(kind_e::wer_kernel));
    if(not crashes.empty())
      {
      std::vector<std::pair<std::string, int>> modules;
      for(record_t const * r: crashes)
        {
        std::string const m{value(*r, 3)};
        auto const at{std::ranges::find_if(modules, [&](auto const & p) { return equal_ascii_fold(p.first, m); })};
        if(at == modules.end())
          modules.emplace_back(m, 1);
        else
          ++at->second;
        }
      auto const most{std::ranges::max_element(modules, {}, &std::pair<std::string, int>::second)};
      out += format("  the module in most game crashes: %s (%d of %zu)\n", most->first.c_str(), most->second,
                    crashes.size());
      }
    for(source_t const & s: in.sources)
      if(s.at_limit)
        out += "  (" + s.name + " was read up to its limit: the counts may be more)\n";

    int section{};
    auto const title = [&](char const * text, std::size_t n)
      { add_title(out, format("%d. %s", ++section, text), n); };

    title("RESTARTS WITHOUT A CLEAN SHUTDOWN AND BLUE SCREENS", restarts.size());
    for(std::size_t i{}; i != std::min(restarts.size(), shown); ++i)
      {
      record_t const & r{*restarts[i]};
      if(r.kind == kind_e::restart)
        out += line_start(r) + "restart without a clean shutdown (Kernel-Power 41), stop code "
             + code_text(value(r, 0), code_e::stop_code) + "\n";
      else if(r.kind == kind_e::blue_screen)
        {
        std::string const v{value(r, 0)};
        out += line_start(r) + "blue screen, stop code " + code_text(v.substr(0, v.find(' ')), code_e::stop_code)
             + "\n";
        }
      else
        out += line_start(r) + "the shutdown before was unexpected (EventLog 6008)\n";
      }

    title("GAME CRASHES (Application Error 1000)", crashes.size());
    for(std::size_t i{}; i != std::min(crashes.size(), shown); ++i)
      {
      record_t const & r{*crashes[i]};
      out += line_start(r) + value(r, 0) + " " + value(r, 1) + "\n";
      out += "     module:    " + value(r, 3) + " " + value(r, 4) + "\n";
      out += "     exception: " + code_text(value(r, 6), code_e::exception) + "\n";
      out += "     offset:    " + value(r, 7) + "\n";
      std::string const id{value(r, 12)};
      out += "     report id: " + (id.empty() ? std::string{"-"} : id) + "\n";
      if(wer_t const * w{find_report(in, id)})
        add_modules(out, *w, in.windows_dir);
      else
        out += "     files loaded into it: no saved report of Windows found for it\n";
      }

    title("GAME HANGS (Application Hang 1002)", hangs.size());
    for(std::size_t i{}; i != std::min(hangs.size(), shown); ++i)
      out += line_start(*hangs[i]) + value(*hangs[i], 0) + " " + value(*hangs[i], 1) + " stopped responding\n";

    title("LAUNCHER CRASHES (Application Error 1000, .NET Runtime 1026)", launcher.size());
    for(std::size_t i{}; i != std::min(launcher.size(), shown); ++i)
      {
      record_t const & r{*launcher[i]};
      if(r.id == 1000)
        out += line_start(r) + value(r, 0) + " " + value(r, 1) + ", module " + value(r, 3) + ", exception "
             + code_text(value(r, 6), code_e::exception) + "\n";
      else
        out += line_start(r) + "EDLaunch.exe, .NET exception: "
             + dotnet_exception(r.values.empty() ? std::string_view{} : std::string_view{r.values[0]}) + "\n";
      }

    title("GRAPHICS DRIVER (System log; graphics hangs Windows reported)", graphics.size());
    for(std::size_t i{}; i != std::min(graphics.size(), shown); ++i)
      {
      record_t const & r{*graphics[i]};
      if(r.kind == kind_e::display_reset)
        out += line_start(r) + "Display 4101: the display driver " + value(r, 0)
             + " stopped responding and recovered\n";
      else if(r.kind == kind_e::wer_kernel)
        out += line_start(r) + "LiveKernelEvent " + code_text(value(r, 5), code_e::stop_code_hex) + "\n";
      else
        out += line_start(r) + r.provider + format(" %u: ", r.id) + value(r, 0) + "\n";
      }

    title("OTHER REPORTS OF THE GAME TO WINDOWS (Windows Error Reporting 1001)", other.size());
    for(std::size_t i{}; i != std::min(other.size(), shown); ++i)
      {
      record_t const & r{*other[i]};
      out += line_start(r) + value(r, 2) + ": " + value(r, 5) + " " + value(r, 6) + " " + value(r, 7) + "\n";
      }

    // a saved report shown with a crash above is not shown again; one of a crash not shown (over the limit) is
    std::vector<wer_t const *> unlinked;
    for(wer_t const & w: in.reports)
      {
      bool linked{};
      for(std::size_t i{}; i != std::min(crashes.size(), shown); ++i)
        linked = linked or wer_has_id(w, value(*crashes[i], 12));
      if(not linked)
        unlinked.push_back(&w);
      }
    std::ranges::stable_sort(unlinked, [](wer_t const * a, wer_t const * b) { return later(a->time, b->time); });
    title("SAVED REPORTS OF WINDOWS WITH NO GAME CRASH ABOVE", unlinked.size());
    for(std::size_t i{}; i != std::min(unlinked.size(), shown); ++i)
      {
      wer_t const & w{*unlinked[i]};
      out += "  " + time_text(w.time) + "  " + cut(w.event_type);
      if(not wer_program(w).empty())
        out += " " + cut(wer_program(w));
      out += "\n";
      for(std::size_t s{}; s != std::min<std::size_t>(w.signature.size(), 8); ++s)
        {
        // a path: its file name only
        std::string_view const v{w.signature[s].second};
        bool const path{v.find_first_of("\\/") != std::string_view::npos};
        out += "     " + cut(w.signature[s].first) + ": "
             + (path ? cut(file_name_of(v)) + " (its folder left out)" : cut(v)) + "\n";
        }
      add_modules(out, w, in.windows_dir);
      }

    std::vector<dump_t const *> dumps;
    for(dump_t const & d: in.dumps)
      dumps.push_back(&d);
    std::ranges::stable_sort(dumps, [](dump_t const * a, dump_t const * b) { return later(a->time, b->time); });
    title("CRASH DUMPS (not included; ask before sending one)", dumps.size());
    for(std::size_t i{}; i != std::min(dumps.size(), shown); ++i)
      {
      dump_t const & d{*dumps[i]};
      out += "  " + time_text(d.time) + "  " + cut(d.name);
      if(not d.bytes)
        out += "  its size not read\n";
      else if(*d.bytes < (1u << 20))
        out += "  under 1 MB\n";
      else
        out += format("  %llu MB\n", static_cast<unsigned long long>((*d.bytes + (1u << 19)) >> 20));
      }

    out += "\nWHAT WAS READ\n";
    for(source_t const & s: in.sources)
      if(s.failure.empty())
        out += "  " + s.name + format(": %d read", s.read) + (s.at_limit ? " (its limit)" : "") + "\n";
    std::size_t failed{};
    for(source_t const & s: in.sources)
      failed += s.failure.empty() ? 0 : 1;
    out += "Checks that could not run:" + std::string{failed == 0 ? " none\n" : "\n"};
    for(source_t const & s: in.sources)
      if(not s.failure.empty())
        out += "  " + s.name + ": " + s.failure + "\n";
    out += "Skipped:" + std::string{in.skipped.empty() ? " none\n" : "\n"};
    for(std::string const & s: in.skipped)
      out += "  " + s + "\n";
    for(std::string const & n: in.notes)
      out += "Note: " + n + "\n";
    return out;
    }

  auto head(input_t const & in, int replaced) -> std::string
    {
    std::string out;
    out += "event_report 1 - Elite Dangerous events from Windows' logs (" + time_text(in.now) + " "
         + offset_text(in.now.offset_minutes) + ", Windows " + in.windows_version + ")\n\n";
    out += "What this program checks:\n";
    out += "  - Windows' Application log: crashes and hangs of EliteDangerous64.exe and EDLaunch.exe\n";
    out += "  - Windows' System log: graphics driver resets and errors, restarts without a clean shutdown and blue "
           "screens\n";
    out += "  - Windows Error Reporting's saved reports of the game: the files loaded into the game when it crashed\n";
    out += "  - the crash dump folders: only the names, sizes and dates of the files\n";
    out += format("  Period: the last %d days; the newest %zu entries of each kind are shown.\n", days, shown);
    out += "What it writes: this text only, to a file of its own beside the program. It holds the Windows\n";
    out += "version, the game's and files' versions, graphics driver names, error codes and times, Windows'\n";
    out += "report ids and names of loaded files - no paths. User, computer and domain names, e-mail addresses\n";
    out += format("and account ids were replaced where found in what was read (%d replaced). Nothing is sent\n"
                  "anywhere: read it, then send it back yourself.\n\n",
                  replaced);
    return out;
    }

  auto clean_input(input_t & in, std::span<token_t const> tokens, fold_equal_t equal) -> int
    {
    int replaced{};
    auto const one = [&](std::string & text)
      {
      cleaned_t c{clean(text, tokens, equal)};
      replaced += c.replaced;
      text = std::move(c.text);
      };
    for(record_t & r: in.records)
      {
      one(r.provider);
      for(std::string & v: r.values)
        one(v);
      }
    for(wer_t & w: in.reports)
      {
      for(std::string * const field: {&w.event_type, &w.report_id, &w.integrator_report_id, &w.app_name, &w.app_path})
        one(*field);
      for(auto & [name, value]: w.signature)
        {
        one(name);
        one(value);
        }
      for(std::string & m: w.modules)
        one(m);
      }
    for(dump_t & d: in.dumps)
      one(d.name);
    // cleaned as the paths are, so they keep their start; not in the report, its replacements not counted
    in.windows_dir = clean(in.windows_dir, tokens, equal).text;
    return replaced;
    }

  auto report(input_t const & in, std::span<token_t const> tokens, fold_equal_t equal) -> std::string
    {
    input_t cleaned{in};
    int const replaced{clean_input(cleaned, tokens, equal)};
    return escape_controls(head(in, replaced)) + escape_controls(body(cleaned));
    }

  auto result_name(local_time_t const & t, int n) -> std::string
    {
    std::string name{
      format("event_report-%04d%02d%02d-%02d%02d%02d", t.year, t.month, t.day, t.hour, t.minute, t.second)};
    if(n > 1)
      name += format("-%d", n);
    return name + ".txt";
    }
  }  // namespace event_report
