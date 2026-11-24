// event_report - the result file: a new one for each run beside the program, never one written over.
#pragma once

#include "report.h"

#include <windows.h>

#include <string>

namespace event_report
  {
  ///\brief `text` to a new file in `dir`, "event_report-<date>-<time>[-n].txt" (CREATE_NEW: an earlier run's result is
  /// never replaced; n up to 9 within one second); its name, empty when it could not be written (`error` set)
  [[nodiscard]]
  auto write_result(std::wstring const & dir, std::string const & text, local_time_t const & now, DWORD & error)
    -> std::string;
  }  // namespace event_report
