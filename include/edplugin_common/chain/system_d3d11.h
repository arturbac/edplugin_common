// edplugin_common - the system's d3d11.dll, loaded by its full path from the system directory (never by name: under
// a loader or a proxy chain "d3d11.dll" by name is the game's own copy, a plugin or edloader), and the range of its
// image: hook/watch.h tells an entry in the runtime from one in another module by it. Loaded once in the process, its
// reference kept to the end of the process (the context's hooks may point into it: contract c).
#pragma once

#include <windows.h>

#include <cstdint>

namespace edplugin_common::chain
  {
  struct system_d3d11_t
    {
    HMODULE module{};       ///< nullptr when it could not be loaded
    std::uint64_t first{};  ///< the image's first byte
    std::uint64_t end{};    ///< one past the image's last byte; first == end when the headers could not be read
    DWORD error{};          ///< GetLastError() of GetSystemDirectoryW or LoadLibraryW when `module` is nullptr

    [[nodiscard]]
    auto contains(std::uint64_t address) const noexcept -> bool
      {
      return address >= first and address < end;
      }
    };

  ///\brief the system's d3d11.dll and its image, loaded at the first call (from any thread) and the same at every next;
  /// the first call not from DllMain (LoadLibraryW under the loader lock)
  [[nodiscard]]
  auto system_d3d11() noexcept -> system_d3d11_t const &;
  }  // namespace edplugin_common::chain
