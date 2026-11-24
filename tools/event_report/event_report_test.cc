// event_report's part without Windows under Windows' own comparison of names (tools/test.sh runs it under wine): the
// checks of report_checks.h, with a name outside ASCII in another case too; and the result file's names: three runs
// in one second in a folder of their own (result_test\ in the current one), each a file of its own, none written
// over.
#include "fold.h"
#include "report_checks.h"
#include "result_file.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace
  {
  int failures{};

  auto check(bool ok, char const * what) -> void
    {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    failures += ok ? 0 : 1;
    }
  ///\brief the file's bytes, empty when it cannot be read
  [[nodiscard]]
  auto read_all(std::wstring const & path) -> std::string
    {
    std::string text;
    HANDLE const f{
      CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if(f == INVALID_HANDLE_VALUE)
      return text;
    char buffer[256];
    DWORD read{};
    while(ReadFile(f, buffer, sizeof buffer, &read, nullptr) and read != 0)
      text.append(buffer, read);
    CloseHandle(f);
    return text;
    }

  auto result_file_checks() -> void
    {
    std::wstring const dir{L"result_test"};
    check(CreateDirectoryW(dir.c_str(), nullptr) != FALSE,
          "result_test\\ made (a new folder: run in a new run directory)");
    event_report::local_time_t const t{2026, 11, 24, 9, 5, 7, 60};
    DWORD error{};
    std::string const first{event_report::write_result(dir, "first\r\n", t, error)};
    std::string const second{event_report::write_result(dir, "second\r\n", t, error)};
    std::string const third{event_report::write_result(dir, "third\r\n", t, error)};
    std::printf("      result files: %s, %s, %s\n", first.c_str(), second.c_str(), third.c_str());
    check(first == "event_report-20261124-090507.txt" and second == "event_report-20261124-090507-2.txt"
            and third == "event_report-20261124-090507-3.txt",
          "write_result: three runs in one second, three names (-2, -3)");
    check(read_all(dir + L"\\event_report-20261124-090507.txt") == "first\r\n"
            and read_all(dir + L"\\event_report-20261124-090507-2.txt") == "second\r\n"
            and read_all(dir + L"\\event_report-20261124-090507-3.txt") == "third\r\n",
          "write_result: each file its own text, the first not written over");
    std::string const none{event_report::write_result(L"result_test\\no_such_folder", "x", t, error)};
    check(none.empty() and error == ERROR_PATH_NOT_FOUND,
          "write_result: a folder that is not there gives no name and its error");
    }
  }  // namespace

auto main() -> int
  {
  event_report::report_checks(check, event_report::equal_windows, true);
  result_file_checks();
  std::printf(failures ? "FAILED (%d failure(s))\n" : "PASSED (%d failure(s))\n", failures);
  return failures ? 1 : 0;
  }
