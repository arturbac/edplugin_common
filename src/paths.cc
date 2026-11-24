// edplugin_common - paths (include/edplugin_common/paths/paths.h).
#include <edplugin_common/paths/paths.h>

#include <edplugin_common/text/format.h>

#include <windows.h>

#include <cstddef>

namespace edplugin_common
  {
  namespace
    {
    // the game's build, told once (InitOnceExecuteOnce publishes it to every thread)
    constinit INIT_ONCE build_once = INIT_ONCE_STATIC_INIT;
    constinit char build_key[48]{};
    constinit std::size_t build_key_size{};
    }  // namespace

  auto environment(wchar_t const * name) noexcept -> std::wstring
    {
    try
      {
      wchar_t small[MAX_PATH];
      DWORD n{GetEnvironmentVariableW(name, small, MAX_PATH)};
      if(n == 0)
        return {};
      if(n < MAX_PATH)
        return std::wstring{small, n};
      // too long for the small buffer: n is the size needed, with the NUL
      std::wstring value(n, L'\0');
      n = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
      value.resize(n < value.size() ? n : 0);
      return value;
      }
    catch(...)
      {
      return {};
      }
    }

  auto module_path(HMODULE module) noexcept -> std::wstring
    {
    try
      {
      wchar_t small[MAX_PATH];
      DWORD const n{GetModuleFileNameW(module, small, MAX_PATH)};
      if(n == 0)
        return {};
      if(n < MAX_PATH)
        return std::wstring{small, n};
      // cut at MAX_PATH: the long form's 32767 characters, only for such a path
      std::wstring path(32768, L'\0');
      DWORD const m{GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()))};
      path.resize(m < path.size() ? m : 0);
      path.shrink_to_fit();  // not 64 KiB held for a path of a few hundred characters
      return path;
      }
    catch(...)
      {
      return {};
      }
    }

  auto directory_of(std::wstring const & path) noexcept -> std::wstring
    {
    try
      {
      auto const slash{path.find_last_of(L"\\/")};
      return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
      }
    catch(...)
      {
      return {};
      }
    }

  auto file_name_of(std::wstring const & path) noexcept -> std::wstring
    {
    try
      {
      auto const slash{path.find_last_of(L"\\/")};
      return slash == std::wstring::npos ? path : path.substr(slash + 1);
      }
    catch(...)
      {
      return {};
      }
    }

  auto plugin_dir(wchar_t const * variable, HMODULE self) noexcept -> std::wstring
    {
    std::wstring dir{environment(variable)};
    return dir.empty() ? directory_of(module_path(self)) : dir;
    }

  auto game_build_key() noexcept -> std::string_view
    {
    InitOnceExecuteOnce(
      &build_once,
      [](PINIT_ONCE, PVOID, PVOID *) -> BOOL
      {
        std::wstring const path{module_path(nullptr)};  // a long path too
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if(not path.empty() and GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a))
          build_key_size = try_format_into(build_key, "{}-{:08x}{:08x}", a.nFileSizeLow, as_unsigned(a.ftLastWriteTime.dwHighDateTime),
                                           as_unsigned(a.ftLastWriteTime.dwLowDateTime))
                             .value_or(0);
        return TRUE;
      },
      nullptr, nullptr);
    return std::string_view{build_key, build_key_size};
    }
  }  // namespace edplugin_common
