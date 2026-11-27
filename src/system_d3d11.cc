// edplugin_common - the system's d3d11.dll (include/edplugin_common/chain/system_d3d11.h).
#include <edplugin_common/chain/system_d3d11.h>

#include <edplugin_common/memory/pe.h>

#include <windows.h>

#include <cwchar>
#include <optional>

namespace edplugin_common::chain
  {
  namespace
    {
    [[nodiscard]]
    auto load() noexcept -> system_d3d11_t
      {
      system_d3d11_t found{};
      wchar_t constexpr name[]{L"\\d3d11.dll"};
      std::size_t constexpr name_length{sizeof(name) / sizeof(wchar_t) - 1};
      wchar_t path[MAX_PATH + name_length]{};
      UINT const length{GetSystemDirectoryW(path, MAX_PATH)};
      if(length == 0 or length >= MAX_PATH)
        {
        // 0: the error in GetLastError(); MAX_PATH or more: the buffer was short, the call set no error
        found.error = length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        return found;
        }
      std::wmemcpy(path + length, name, name_length + 1);
      found.module = LoadLibraryW(path);
      if(not found.module)
        {
        found.error = GetLastError();
        return found;
        }
      if(std::optional<pe_image_t> const image{pe_image(found.module)}; image)
        {
        found.first = reinterpret_cast<std::uintptr_t>(image->base());
        found.end = found.first + image->size();
        }
      return found;
      }
    }  // namespace

  auto system_d3d11() noexcept -> system_d3d11_t const &
    {
    // load() throws nothing: the static's start is noexcept, and at most one thread runs it
    static system_d3d11_t const found{load()};
    return found;
    }
  }  // namespace edplugin_common::chain
