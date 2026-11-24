// edplugin_common - the names shaders go by: FNV-1a 64 over the bytecode (EDVR's, the plugins' own) and 3DMigoto's
// FNV-1 (the names in ShaderFixes and in d3dx.ini's [ShaderOverride-...]). No Windows header: testable anywhere.
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>

namespace edplugin_common
  {
  ///\brief the basis the plugins' names start from: 1469598103934665603, not FNV's published 14695981039346656037 (a
  /// digit short). Every name in the plugins' catalogs, lists and tools is made with it: it stays
  inline constexpr std::uint64_t fnv1a64_basis{1469598103934665603ull};
  inline constexpr std::uint64_t fnv64_prime{1099511628211ull};

  ///\brief FNV-1a 64 over `bytes` bytes, going on from `h` (the basis for a new hash; a hash so far to go on over
  /// more bytes)
  [[nodiscard]]
  constexpr auto fnv1a64(std::uint8_t const * data, std::size_t bytes, std::uint64_t h = fnv1a64_basis) noexcept -> std::uint64_t
    {
    for(std::size_t i{}; i != bytes; ++i)
      {
      h ^= data[i];
      h *= fnv64_prime;
      }
    return h;
    }

  [[nodiscard]]
  inline auto fnv1a64(void const * data, std::size_t bytes, std::uint64_t h = fnv1a64_basis) noexcept -> std::uint64_t
    {
    return fnv1a64(static_cast<std::uint8_t const *>(data), bytes, h);
    }

  ///\brief 3DMigoto's hash (its fnv_64_buf): FNV-1 from a basis of 0, the multiplication before the xor
  [[nodiscard]]
  constexpr auto fnv_3dmigoto(std::uint8_t const * data, std::size_t bytes, std::uint64_t h = 0) noexcept -> std::uint64_t
    {
    for(std::size_t i{}; i != bytes; ++i)
      {
      h *= fnv64_prime;
      h ^= data[i];
      }
    return h;
    }

  [[nodiscard]]
  inline auto fnv_3dmigoto(void const * data, std::size_t bytes, std::uint64_t h = 0) noexcept -> std::uint64_t
    {
    return fnv_3dmigoto(static_cast<std::uint8_t const *>(data), bytes, h);
    }

  ///\brief 3DMigoto's name of a shader, `3dmgt_` and 16 hex digits, in `text`
  inline auto migoto_name_text(std::uint64_t name, char (&text)[24]) noexcept -> std::string_view
    {
    auto const end{std::format_to_n(text, sizeof text - 1, "3dmgt_{:016x}", static_cast<unsigned long long>(name)).out};
    *end = '\0';
    return std::string_view{text, static_cast<std::size_t>(end - text)};
    }

  namespace detail
    {
    inline constexpr std::uint8_t letter_a[]{'a'};
    inline constexpr std::uint8_t letters_ab[]{'a', 'b'};
    }

  static_assert(fnv1a64(detail::letter_a, 0) == 0x14650fb0739d0383ull, "FNV-1a 64 of nothing is its basis");
  static_assert(fnv1a64(detail::letter_a, 1) == 0x44bd8ad473cd9906ull, "FNV-1a 64 of \"a\" from the plugins' basis");
  static_assert(fnv_3dmigoto(detail::letter_a, 0) == 0, "3DMigoto's hash of nothing is 0");
  static_assert(fnv_3dmigoto(detail::letter_a, 1) == 0x61ull, "3DMigoto's hash of \"a\"");
  static_assert(fnv_3dmigoto(detail::letters_ab, 2) == 0x61000000a4b1ull, "3DMigoto's hash of \"ab\"");
  }  // namespace edplugin_common
