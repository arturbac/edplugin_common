// edplugin_common - the modules passed over (include/edplugin_common/memory/pass_modules.h).
#include <edplugin_common/memory/pass_modules.h>

#include <windows.h>

#include <limits>

namespace edplugin_common
  {
  auto passed_over(std::wstring_view file, std::wstring_view list) noexcept -> bool
    {
    if(file.empty() or file.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      return false;
    while(not list.empty())
      {
      std::wstring_view const name{next_pass_name(list)};
      if(name.size() == file.size()
         and CompareStringOrdinal(name.data(), static_cast<int>(name.size()), file.data(), static_cast<int>(file.size()), TRUE)
               == CSTR_EQUAL)
        return true;
      }
    return false;
    }
  }  // namespace edplugin_common
