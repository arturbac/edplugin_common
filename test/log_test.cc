// The checks of the modules log (log_open and its setting aside, lines, the tap, batches, log_line_to, log_close) and
// state_log_t, in the directory `log` under the current one (tools/test.sh runs the program in a new directory).
#include <edplugin_common/log/log.h>
#include <edplugin_common/log/state_log.h>
#include <edplugin_common/text/format.h>

#include <windows.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace
  {
  ///\brief a value whose formatting throws, for a line that cannot be made
  struct log_throws_t
    {
    };
  }  // namespace

template<>
struct std::formatter<log_throws_t, char> : std::formatter<std::string_view, char>
  {
  auto format(log_throws_t const &, std::format_context &) const -> std::format_context::iterator
    {
    throw std::runtime_error{"no text"};
    }
  };

namespace
  {
  using check_t = void (*)(bool ok, char const * what);
  check_t check{};
  std::wstring root;

  auto make_dir(std::wstring const & dir) -> std::wstring
    {
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
    }

  auto case_dir(std::wstring_view name) -> std::wstring { return make_dir(root + L'\\' + std::wstring{name}); }

  auto read_file(std::wstring const & path) -> std::string
    {
    HANDLE const f{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr)};
    if(f == INVALID_HANDLE_VALUE)
      return {};
    std::string text;
    char buf[65536];
    DWORD got{};
    while(ReadFile(f, buf, sizeof buf, &got, nullptr) and got != 0)
      text.append(buf, got);
    CloseHandle(f);
    return text;
    }

  auto write_file(std::wstring const & path, std::string_view text) -> void
    {
    HANDLE const f{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    DWORD written{};
    WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(f);
    }

  auto exists(std::wstring const & path) -> bool { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

  ///\brief the name a file is set aside under: <name>.<UTC of its last write>.log
  auto aside_name(std::wstring const & path, std::wstring_view name) -> std::wstring
    {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a);
    SYSTEMTIME t{};
    FileTimeToSystemTime(&a.ftLastWriteTime, &t);
    return std::format(L"{}.{:04}{:02}{:02}T{:02}{:02}{:02}Z.log", name, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    }

  auto has(std::string_view text, std::string_view part) -> bool { return text.find(part) != std::string_view::npos; }

  ///\brief the lines of a text, each without its line end; a last piece without one is a line too
  auto lines_of(std::string_view text) -> std::vector<std::string_view>
    {
    std::vector<std::string_view> lines;
    while(not text.empty())
      {
      std::size_t const end{text.find('\n')};
      lines.push_back(text.substr(0, end));
      text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
      }
    return lines;
    }

  auto opening() -> void
    {
    std::wstring const dir{case_dir(L"new")};
    check(not edplugin_common::log_is_open(), "log: not open before log_open");
    check(edplugin_common::log_open(dir, L"p"), "log_open: a new file");
    check(edplugin_common::log_is_open(), "log_is_open: open after log_open");
    edplugin_common::log_line("hello {}", 1);
    check(edplugin_common::log_open(dir, L"p"), "log_open: a second call while open is true");
    edplugin_common::log_line("again");
    edplugin_common::log_close();
    check(not edplugin_common::log_is_open(), "log_is_open: not open after log_close");
    std::string const text{read_file(dir + L"\\p.log")};
    std::vector<std::string_view> const lines{lines_of(text)};
    check(lines.size() == 2 and lines[0].size() == 24 + 1 + 7 and lines[0].substr(25) == "hello 1" and lines[0][10] == 'T'
            and lines[0][19] == '.' and lines[0][23] == 'Z' and text.back() == '\n',
          "log_line: a UTC stamp with milliseconds, the text, a line end");
    check(lines.size() == 2 and lines[1].substr(25) == "again" and not exists(dir + L"\\p"), "log_open: a second call moves nothing");
    edplugin_common::log_line("closed");
    check(read_file(dir + L"\\p.log") == text, "log_line after log_close writes nothing");
    }

  auto setting_aside() -> void
    {
    std::wstring const dir{case_dir(L"aside")};
    std::wstring const path{dir + L"\\p.log"};
    write_file(path, "old\n");
    std::wstring const target{dir + L"\\p\\" + aside_name(path, L"p")};
    check(edplugin_common::log_open(dir, L"p"), "log_open: with the last session's file");
    edplugin_common::log_line("new");
    edplugin_common::log_close();
    check(read_file(target) == "old\n", "log_open: the last session set aside in <name>\\<name>.<UTC>.log");
    check(lines_of(read_file(path)).size() == 1 and read_file(path).ends_with(" new\n"), "log_open: this session in a new file");

    std::wstring const empty_dir{case_dir(L"empty-file")};
    write_file(empty_dir + L"\\p.log", "");
    check(edplugin_common::log_open(empty_dir, L"p") and not exists(empty_dir + L"\\p"), "log_open: an empty file is not set aside");
    edplugin_common::log_close();
    }

  auto not_set_aside() -> void
    {
    // the target's name taken: MoveFileExW fails, this session appends
    std::wstring dir{case_dir(L"taken")};
    std::wstring path{dir + L"\\p.log"};
    write_file(path, "old\n");
    make_dir(dir + L"\\p");
    write_file(dir + L"\\p\\" + aside_name(path, L"p"), "older\n");
    check(edplugin_common::log_open(dir, L"p"), "log_open: target taken, still open");
    edplugin_common::log_close();
    std::string text{read_file(path)};
    check(text.starts_with("old\n") and has(text, "could not be set aside") and (has(text, "(error 80)") or has(text, "(error 183)")),
          "log_open: target taken - appending, the code of MoveFileExW in the line");

    // a file where the directory would be
    dir = case_dir(L"not-dir");
    path = dir + L"\\p.log";
    write_file(path, "old\n");
    write_file(dir + L"\\p", "a file\n");
    check(edplugin_common::log_open(dir, L"p"), "log_open: a file in the directory's place, still open");
    edplugin_common::log_close();
    text = read_file(path);
    check(text.starts_with("old\n") and has(text, "(error 183, not a directory)"),
          "log_open: a file in the directory's place - appending, 183, not a directory");

    // a path of the log within 259 characters, and the directory set aside (`<dir>\p`, 2 more) over 247, or only the
    // file moved into it (`<dir>\p\p.<UTC>.log`, 25 more) over 259: appending, 206
    for(std::size_t const length: {246u, 240u})
      {
      if(root.size() + 2 > length)
        {
        check(false, "log_open: the test's directory too long for the deep directories");
        return;
        }
      std::wstring deep{make_dir(root + L"\\deep" + std::to_wstring(length))};
      while(deep.size() + 1 + 200 < length)
        deep = make_dir(deep + L'\\' + std::wstring(200, L'd'));
      deep = make_dir(deep + L'\\' + std::wstring(length - deep.size() - 1, L'e'));
      path = deep + L"\\p.log";
      write_file(path, "old\n");
      check(deep.size() == length and exists(path), length == 246u ? "log_open: a deep directory made (246 characters)"
                                                                    : "log_open: a deep directory made (240 characters)");
      check(edplugin_common::log_open(deep, L"p"), "log_open: a path set aside too long, still open");
      edplugin_common::log_close();
      text = read_file(path);
      check(text.starts_with("old\n") and has(text, "(error 206)"),
            length == 246u ? "log_open: the directory set aside over 247 characters - appending, 206"
                           : "log_open: only the file moved over 259 characters - appending, 206");
      }
    }

  auto refused() -> void
    {
    check(not edplugin_common::log_open(L"", L"p") and not edplugin_common::log_is_open(), "log_open: an empty directory is false");
    std::wstring const long_dir{root + L'\\' + std::wstring(255, L'x')};
    check(not edplugin_common::log_open(long_dir, L"p") and not edplugin_common::log_is_open(),
          "log_open: a log's path over 259 characters is false");
    check(not edplugin_common::log_open(root, L"") and not edplugin_common::log_is_open(), "log_open: an empty name is false");
    check(not edplugin_common::log_open(root + L"\\no such directory", L"p") and not edplugin_common::log_is_open(),
          "log_open: a file that cannot be opened is false");
    }

  auto line_texts() -> void
    {
    std::wstring const dir{case_dir(L"lines")};
    edplugin_common::log_open(dir, L"p");
    edplugin_common::log_line("{}", std::string(5000, 'x'));
    edplugin_common::log_line("a{}b", log_throws_t{});
    edplugin_common::log_close();
    std::vector<std::string_view> const lines{lines_of(read_file(dir + L"\\p.log"))};
    check(lines.size() == 2 and lines[0].size() == 2047 and lines[0].find_first_not_of('x', 25) == std::string_view::npos,
          "log_line: cut to 2 KB with its line end");
    check(lines.size() == 2 and lines[1].substr(25) == "a (log: the rest of this line could not be formatted)",
          "log_line: a text that cannot be made says so");

    std::wstring const other{dir + L"\\other.txt"};
    edplugin_common::log_line_to(other, "one {}", 1);
    edplugin_common::log_line_to(other, "two");
    std::vector<std::string_view> const appended{lines_of(read_file(other))};
    check(appended.size() == 2 and appended[0].substr(25) == "one 1" and appended[1].substr(25) == "two" and appended[1][23] == 'Z',
          "log_line_to: stamped lines appended to the file, the log closed");
    }

  int taps{};
  std::string tapped_text;

  auto tap(std::string_view text) noexcept -> void
    {
    ++taps;
    tapped_text.assign(text);
    }

  template<typename... args_t>
  auto tapped(std::format_string<args_t...> fmt, args_t &&... args) -> void
    {
    edplugin_common::log_vline(fmt.get(), std::make_format_args(args...), tap);
    }

  auto tapping() -> void
    {
    std::wstring const dir{case_dir(L"tap")};
    tapped("closed {}", 1);
    check(taps == 1 and tapped_text == "closed 1", "tap: a line without an open log");
    edplugin_common::log_open(dir, L"p");
    tapped("open {}", 2);
    check(taps == 2 and tapped_text == "open 2", "tap: a line with the log open");
    edplugin_common::log_batch_begin();
    tapped("batch {}", 3);
    check(taps == 3 and tapped_text == "batch 3", "tap: a line in a batch, before the batch is written");
    edplugin_common::log_batch_end();
    tapped("x{}", log_throws_t{});
    check(taps == 4 and tapped_text == "x (log: the rest of this line could not be formatted)", "tap: a text that cannot be made, once");
    edplugin_common::log_close();
    std::vector<std::string_view> const lines{lines_of(read_file(dir + L"\\p.log"))};
    check(lines.size() == 3 and lines[0].substr(25) == "open 2" and lines[1].substr(25) == "batch 3", "tap: the lines in the log as well");
    }

  std::wstring reentry_path;
  bool in_file_at_tap{true};

  ///\brief a tap that reads the log and logs a line itself: under the log's lock that line would wait for good
  auto reentering_tap(std::string_view text) noexcept -> void
    {
    try
      {
      in_file_at_tap = has(read_file(reentry_path), text);
      }
    catch(...)
      {
      }
    edplugin_common::log_line("from the tap");
    }

  ///\brief the tap before the line is written and outside the log's lock, in a thread: a wait for the lock ends the test
  auto tap_outside_the_lock() -> void
    {
    std::wstring const dir{case_dir(L"tap-lock")};
    reentry_path = dir + L"\\p.log";
    edplugin_common::log_open(dir, L"p");
    HANDLE const thread{CreateThread(
      nullptr, 0,
      [](void *) -> DWORD
      {
        int const n{5};
        edplugin_common::log_vline("tapped {}", std::make_format_args(n), reentering_tap);
        return 0;
      },
      nullptr, 0, nullptr)};
    if(not thread or WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0)
      {
      check(false, "tap: a line logged from the tap ends in time (5 s), outside the log's lock");
      std::printf("FAILED, stopped at a tap waiting for the log's lock\n");
      ExitProcess(1);
      }
    CloseHandle(thread);
    edplugin_common::log_close();
    std::vector<std::string_view> const lines{lines_of(read_file(reentry_path))};
    check(not in_file_at_tap and lines.size() == 2 and lines[0].substr(25) == "from the tap" and lines[1].substr(25) == "tapped 5",
          "tap: outside the log's lock, before its line is written");
    }

  auto batches() -> void
    {
    std::wstring const dir{case_dir(L"batch")};
    std::wstring const path{dir + L"\\p.log"};
    edplugin_common::log_batch_begin();
    edplugin_common::log_line("before");
    edplugin_common::log_batch_end();
    edplugin_common::log_open(dir, L"p");
    edplugin_common::log_batch_begin();
    edplugin_common::log_line("gathered");
    bool const held{read_file(path).empty()};
    edplugin_common::log_batch_begin();  // no nesting: nothing
    edplugin_common::log_line("gathered too");
    edplugin_common::log_batch_end();
    std::string const text{read_file(path)};
    check(held and lines_of(text).size() == 2 and has(text, " gathered\n"), "batch: written at its end, the first end ends it");
    edplugin_common::log_line("at once");
    check(lines_of(read_file(path)).size() == 3, "batch: a line after the end at once");
    edplugin_common::log_batch_end();  // no batch: nothing
    edplugin_common::log_close();
    check(not has(read_file(path), "before"), "batch: nothing gathered without an open log");
    }

  ///\brief 4 threads, each 1000 batches of 1 to 24 numbered lines of about 1 KB (some fill the 16 KB batch)
  auto stress() -> void
    {
    constexpr int threads{4};
    constexpr int cycles{1000};
    std::wstring const dir{case_dir(L"stress")};
    edplugin_common::log_open(dir, L"p");
    auto const start{std::chrono::steady_clock::now()};
    HANDLE handles[threads]{};
    for(int t{}; t < threads; ++t)
      handles[t] = CreateThread(
        nullptr, 0,
        [](void * arg) -> DWORD
        {
          int const t{static_cast<int>(reinterpret_cast<std::intptr_t>(arg))};
          std::string const pad(1000, static_cast<char>('a' + t));
          int n{};
          for(int c{}; c < cycles; ++c)
            {
            edplugin_common::log_batch_begin();
            for(int k{}, lines{1 + c % 24}; k < lines; ++k)
              edplugin_common::log_line("T{} N{} {}|", t, n++, pad);
            edplugin_common::log_batch_end();
            }
          return 0;
        },
        reinterpret_cast<void *>(static_cast<std::intptr_t>(t)), 0, nullptr);
    WaitForMultipleObjects(threads, handles, TRUE, INFINITE);
    for(HANDLE const h: handles)
      CloseHandle(h);
    edplugin_common::log_close();
    double const seconds{std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()};

    int expected_lines{};
    for(int c{}; c < cycles; ++c)
      expected_lines += 1 + c % 24;
    std::vector<std::vector<int>> seen(threads, std::vector<int>(static_cast<std::size_t>(expected_lines)));
    std::string const text{read_file(dir + L"\\p.log")};
    int bad{};
    int total{};
    int out_of_order{};
    std::vector<int> last(threads, -1);
    for(std::string_view const line: lines_of(text))
      {
      ++total;
      int t{-1};
      int n{-1};
      if(line.size() > 27 and line.substr(25, 1) == "T")
        {
        auto const after_t{std::from_chars(line.data() + 26, line.data() + line.size(), t)};
        if(after_t.ec == std::errc{} and line.data() + line.size() - after_t.ptr >= 2 and std::string_view{after_t.ptr, 2} == " N")
          std::from_chars(after_t.ptr + 2, line.data() + line.size(), n);
        }
      if(t < 0 or t >= threads or n < 0 or n >= expected_lines or not line.ends_with(std::string(1000, static_cast<char>('a' + t)) + "|"))
        {
        ++bad;
        continue;
        }
      ++seen[static_cast<std::size_t>(t)][static_cast<std::size_t>(n)];
      out_of_order += n <= last[static_cast<std::size_t>(t)];
      last[static_cast<std::size_t>(t)] = n;
      }
    int not_once{};
    for(auto const & per_thread: seen)
      for(int const count: per_thread)
        not_once += count != 1;
    std::printf("      stress: %d lines, %.1f s\n", total, seconds);
    check(bad == 0 and not_once == 0 and total == threads * expected_lines and text.back() == '\n',
          "batch, 4 threads: every line whole and in the log exactly once");
    check(out_of_order == 0, "batch, 4 threads: each thread's lines in their order, also across a full batch");
    }

  enum struct mode_e : unsigned char
    {
    unknown,
    on,
    off
    };

  constinit edplugin_common::state_log_t<mode_e> global_mode{mode_e::unknown};

  auto states() -> void
    {
    check(global_mode.last() == mode_e::unknown, "state_log_t: constant-initialised with the first value");
    bool const first{global_mode.changed(mode_e::on)};
    bool const same{global_mode.changed(mode_e::on)};
    bool const other{global_mode.changed(mode_e::off)};
    check(first and not same and other and global_mode.last() == mode_e::off, "state_log_t: true on a change only");
    edplugin_common::state_log_t<int> count{-1};
    check(not count.changed(-1) and count.changed(0), "state_log_t: the first value counts as seen");
    }
  }  // namespace

auto log_tests(void (*checker)(bool ok, char const * what)) -> void
  {
  check = checker;
  wchar_t here[MAX_PATH];
  DWORD const n{GetCurrentDirectoryW(MAX_PATH, here)};
  root = make_dir(std::wstring{here, n} + L"\\log");
  opening();
  setting_aside();
  not_set_aside();
  refused();
  line_texts();
  tapping();
  tap_outside_the_lock();
  batches();
  stress();
  states();
  }
