// edplugin_common - the ranges a PE image's offsets must keep to, checked before anything at them is read; no Windows
// header (built natively with sanitizers too, test/native).
#pragma once

#include <cstdint>

namespace edplugin_common
  {
  ///\brief whether `bytes` bytes at `rva` lie within the first `size` bytes, without overflow
  [[nodiscard]]
  constexpr auto rva_in(std::uint64_t rva, std::uint64_t bytes, std::uint64_t size) noexcept -> bool
    {
    return rva <= size and bytes <= size - rva;
    }

  ///\brief whether `count` items of `item` bytes each at `rva` lie within the first `size` bytes, without overflow
  [[nodiscard]]
  constexpr auto array_in(std::uint64_t rva, std::uint64_t count, std::uint64_t item, std::uint64_t size) noexcept -> bool
    {
    return item != 0 and rva <= size and count <= (size - rva) / item;
    }

  ///\brief whether `rva` lies in the `length` bytes from `start` (a forwarder's text inside the export directory)
  [[nodiscard]]
  constexpr auto rva_inside(std::uint64_t rva, std::uint64_t start, std::uint64_t length) noexcept -> bool
    {
    return rva >= start and rva - start < length;
    }

  /// a DOS header's e_lfanew: past the DOS header itself, within the first page
  inline constexpr std::int64_t lfanew_least{64};
  inline constexpr std::int64_t lfanew_most{4096};

  ///\brief whether a DOS header's `lfanew` leads to NT headers of `nt_bytes` bytes inside the first `size` bytes
  [[nodiscard]]
  constexpr auto lfanew_ok(std::int64_t lfanew, std::uint64_t nt_bytes, std::uint64_t size) noexcept -> bool
    {
    return lfanew >= lfanew_least and lfanew <= lfanew_most and rva_in(static_cast<std::uint64_t>(lfanew), nt_bytes, size);
    }

  static_assert(rva_in(0, 0, 0) and rva_in(0x1000, 0x1000, 0x2000) and not rva_in(0x1000, 0x1001, 0x2000));
  static_assert(not rva_in(0x2001, 0, 0x2000) and not rva_in(8, ~std::uint64_t{}, 0x2000), "no overflow of rva + bytes");
  static_assert(array_in(0x100, 4, 4, 0x110) and not array_in(0x100, 5, 4, 0x110) and not array_in(0, 1, 0, 0x100));
  static_assert(not array_in(0, std::uint64_t{1} << 62, 8, 0x1000), "no overflow of count * item");
  static_assert(rva_inside(0x1000, 0x1000, 1) and not rva_inside(0x1001, 0x1000, 1) and not rva_inside(0xFFF, 0x1000, 0x10));
  static_assert(lfanew_ok(64, 264, 0x1000) and lfanew_ok(0xF8, 264, 0x1000) and not lfanew_ok(63, 264, 0x1000));
  static_assert(not lfanew_ok(-8, 264, 0x1000) and not lfanew_ok(4097, 264, 0x2000) and not lfanew_ok(4000, 264, 0x1000));
  }  // namespace edplugin_common
