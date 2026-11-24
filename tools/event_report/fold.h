// event_report - case-insensitive equality of UTF-8 texts as Windows compares names (letters outside ASCII too):
// report.h's fold_equal_t for the program and for event_report_test.exe.
#pragma once

#include <string_view>

namespace event_report
  {
  [[nodiscard]]
  auto equal_windows(std::string_view a, std::string_view b) -> bool;
  }  // namespace event_report
