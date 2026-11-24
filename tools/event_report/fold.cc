// event_report - case-insensitive equality as Windows compares names (fold.h).
#include "fold.h"

#include <windows.h>

#include <string>

namespace event_report
  {
  namespace
    {
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
    }  // namespace

  auto equal_windows(std::string_view a, std::string_view b) -> bool
    {
    std::wstring const wa{wide(a)};
    std::wstring const wb{wide(b)};
    return CompareStringOrdinal(wa.c_str(), int(wa.size()), wb.c_str(), int(wb.size()), TRUE) == CSTR_EQUAL;
    }
  }  // namespace event_report
