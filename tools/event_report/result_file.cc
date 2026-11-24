// event_report - the result file (result_file.h).
#include "result_file.h"

#include <string_view>

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

  auto write_result(std::wstring const & dir, std::string const & text, local_time_t const & now, DWORD & error)
    -> std::string
    {
    error = ERROR_PATH_NOT_FOUND;
    for(int n{1}; not dir.empty() and n != 10; ++n)
      {
      std::string const name{result_name(now, n)};
      HANDLE const f{CreateFileW((dir + L"\\" + wide(name)).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL, nullptr)};
      if(f == INVALID_HANDLE_VALUE)
        {
        error = GetLastError();
        if(error == ERROR_FILE_EXISTS)
          continue;
        return {};
        }
      DWORD written{};
      bool const ok{WriteFile(f, text.data(), DWORD(text.size()), &written, nullptr) and written == text.size()};
      error = ok ? 0 : GetLastError();
      CloseHandle(f);
      if(not ok)
        (void)DeleteFileW((dir + L"\\" + wide(name)).c_str());  // no cut file left under a result's name
      return ok ? name : std::string{};
      }
    return {};
    }
  }  // namespace event_report
