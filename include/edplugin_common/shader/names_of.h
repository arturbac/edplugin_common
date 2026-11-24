// edplugin_common - both names of a shader's bytecode read under a fault handler, for bytes another module hands over.
#pragma once

#include <cstddef>
#include <cstdint>

namespace edplugin_common
  {
  struct names_t
    {
    std::uint64_t name;    ///< FNV-1a 64
    std::uint64_t migoto;  ///< 3DMigoto's
    bool read;             ///< false when the bytes could not be read (a fault, or no bytecode): both names 0
    };

  ///\brief both names of `bytes` bytes of bytecode at `code`
  [[nodiscard]]
  auto names_of(void const * code, std::size_t bytes) noexcept -> names_t;
  }  // namespace edplugin_common
