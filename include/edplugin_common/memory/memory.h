// edplugin_common - memory of the game's process: whether it can be read or holds code, the module an address lies
// in, a loaded module's PE headers and sections, a loaded module's exports by name. Each part is looked at before it
// is read: the game's process, no fault.
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace edplugin_common
  {
  ///\brief whether `bytes` bytes at `p` can be read now: committed, with read access, no guard page, all in one region
  [[nodiscard]]
  auto readable(void const * p, std::size_t bytes) noexcept -> bool;

  ///\brief whether `p` lies in committed memory that holds code (execute access)
  [[nodiscard]]
  auto executable(void const * p) noexcept -> bool;

  ///\brief the loaded module `p` lies in, its reference count unchanged; nullptr when none
  [[nodiscard]]
  auto module_of(void const * p) noexcept -> HMODULE;

  ///\brief a loaded module's PE32+ headers, the DOS and NT headers checked readable and signed; nullptr otherwise
  [[nodiscard]]
  auto pe_headers(HMODULE module) noexcept -> IMAGE_NT_HEADERS64 const *;

  ///\brief the PE section of `module` holding `p` (its name, up to 8 characters), or "-" when none does or the
  /// headers cannot be read
  auto section_of(HMODULE module, void const * p, char (&name)[9]) noexcept -> void;

  enum struct export_scan_e : std::uint8_t
    {
    none,       ///< no export by such a name (or no exports)
    found,      ///< the first such export's name in `name`
    malformed,  ///< the headers or the export directory out of shape
    fault       ///< a fault reading them, its code in `fault`
    };

  ///\brief the first export of a loaded module whose name starts with `prefix`, read from the image's export directory
  /// (each offset checked against the image's size, under a fault handler); its name cut to 63 characters. An empty
  /// prefix finds none
  [[nodiscard]]
  auto first_export_with_prefix(HMODULE module, std::string_view prefix, char (&name)[64], DWORD & fault) noexcept -> export_scan_e;
  }  // namespace edplugin_common
