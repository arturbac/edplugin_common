// event_report's part without Windows under Windows' own comparison of names (tools/test.sh runs it under wine): the
// checks of report_checks.h, with a name outside ASCII in another case too.
#include "fold.h"
#include "report_checks.h"

#include <cstdio>

namespace
  {
  int failures{};

  auto check(bool ok, char const * what) -> void
    {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    failures += ok ? 0 : 1;
    }
  }  // namespace

auto main() -> int
  {
  event_report::report_checks(check, event_report::equal_windows, true);
  std::printf(failures ? "FAILED (%d failure(s))\n" : "PASSED (%d failure(s))\n", failures);
  return failures ? 1 : 0;
  }
