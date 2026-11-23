// edplugin_common - text for std::format: wide strings in narrow (UTF-8) lines and back, values for hexadecimal
// fields, and std::format into a fixed buffer without exceptions.
#pragma once

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace edplugin_common
  {
  ///\brief a wide string in a log line, written as UTF-8 (std::format takes no wide string into a narrow line)
  struct wide_t
    {
    std::wstring_view text;
    };

  [[nodiscard]]
  inline auto wide(std::wstring_view text) noexcept -> wide_t
    {
    return {text};
    }

  [[nodiscard]]
  inline auto wide(wchar_t const * text) noexcept -> wide_t
    {
    return {text ? std::wstring_view{text} : std::wstring_view{L"(null)"}};
    }

  ///\brief a narrow (UTF-8) string in a wide text, as UTF-16 (std::format takes no narrow string into a wide text)
  struct utf8_t
    {
    std::string_view text;
    };

  [[nodiscard]]
  inline auto utf8(std::string_view text) noexcept -> utf8_t
    {
    return {text};
    }

  [[nodiscard]]
  inline auto utf8(char const * text) noexcept -> utf8_t
    {
    return {text ? std::string_view{text} : std::string_view{"(null)"}};
    }

  ///\brief a value for a hexadecimal field: a signed one (HRESULT, LONG) as its bits, as printf's %X showed it
  template<typename value_t>
  [[nodiscard]]
  constexpr auto as_unsigned(value_t value) noexcept
    {
    if constexpr(std::is_enum_v<value_t>)
      return as_unsigned(std::to_underlying(value));
    else if constexpr(std::is_integral_v<value_t> and std::is_signed_v<value_t>)
      return static_cast<std::make_unsigned_t<value_t>>(value);
    else
      return value;
    }

  ///\brief std::format into `size` characters at `buf`, as snprintf did: cut at the end, NUL-terminated; the characters
  /// written (without the NUL), or none when the text could not be made (the buffer then empty). Never thrown; the
  /// caller logs a failure. Not named format_into: a plugin's own format_into, called unqualified with an argument
  /// of this library's types, would find this one too by argument-dependent lookup
  template<typename char_t, typename... args_t>
  [[nodiscard]]
  auto try_format_into(char_t * buf, std::size_t size, std::type_identity_t<std::basic_format_string<char_t, args_t...>> fmt,
                       args_t &&... args) noexcept -> std::optional<std::size_t>
    {
    if(size == 0)
      return std::size_t{};
    try
      {
      std::size_t const n{
        static_cast<std::size_t>(std::format_to_n(buf, static_cast<std::ptrdiff_t>(size - 1), fmt, std::forward<args_t>(args)...).out - buf)};
      buf[n] = char_t{};
      return n;
      }
    catch(...)
      {
      buf[0] = char_t{};
      return std::nullopt;
      }
    }

  template<typename char_t, std::size_t size, typename... args_t>
  [[nodiscard]]
  auto try_format_into(char_t (&buf)[size], std::type_identity_t<std::basic_format_string<char_t, args_t...>> fmt, args_t &&... args) noexcept
    -> std::optional<std::size_t>
    {
    return edplugin_common::try_format_into(&buf[0], size, fmt, std::forward<args_t>(args)...);
    }
  }  // namespace edplugin_common

///\brief a narrow string (edplugin_common::utf8) in a wide text as UTF-16, by pieces through a buffer on the stack
template<>
struct std::formatter<edplugin_common::utf8_t, wchar_t> : std::formatter<std::wstring_view, wchar_t>
  {
  auto format(edplugin_common::utf8_t const & u, std::wformat_context & ctx) const -> std::wformat_context::iterator
    {
    auto out{ctx.out()};
    std::string_view rest{u.text};
    wchar_t buf[256];
    while(not rest.empty())
      {
      std::size_t len{std::min<std::size_t>(rest.size(), 240)};
      // a character's bytes stay whole: at most 3 continuation bytes back (a character has at most 4), so a piece
      // never comes to nothing on bytes that are not UTF-8; those become U+FFFD
      for(int back{}; back < 3 and len < rest.size() and (static_cast<unsigned char>(rest[len]) & 0xC0u) == 0x80u; ++back)
        --len;
      int const n{MultiByteToWideChar(CP_UTF8, 0, rest.data(), static_cast<int>(len), buf, static_cast<int>(std::size(buf)))};
      for(int i{}; i < n; ++i)
        *out++ = buf[i];
      rest.remove_prefix(len);
      }
    return out;
    }
  };

///\brief a wide string (edplugin_common::wide) in a log line as UTF-8, by pieces through a buffer on the stack
template<>
struct std::formatter<edplugin_common::wide_t, char> : std::formatter<std::string_view, char>
  {
  auto format(edplugin_common::wide_t const & w, std::format_context & ctx) const -> std::format_context::iterator
    {
    auto out{ctx.out()};
    std::wstring_view rest{w.text};
    char buf[512];
    while(not rest.empty())
      {
      std::size_t len{std::min<std::size_t>(rest.size(), 120)};
      if(len < rest.size() and rest[len - 1] >= 0xD800 and rest[len - 1] <= 0xDBFF)
        --len;  // a surrogate pair stays whole
      int const n{WideCharToMultiByte(CP_UTF8, 0, rest.data(), static_cast<int>(len), buf, static_cast<int>(sizeof buf), nullptr, nullptr)};
      for(int i{}; i < n; ++i)
        *out++ = buf[i];
      rest.remove_prefix(len);
      }
    return out;
    }
  };
