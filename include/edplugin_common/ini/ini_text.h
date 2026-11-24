// edplugin_common - the text of a plugin's ini, without Windows: a line split into its key and value, a comment cut
// off the value by the value's kind, the readers of one value (a number, a whole number, a switch, hexadecimal names,
// a colour, a list of names, a choice of names), the key a line names (also as a comment), the file's line ends and
// its byte order mark, UTF-8 and UTF-16 both ways. Pure functions, so test/native checks them with sanitizers; the
// table of settings over them is in ini_table.h, the files in ini.h.
//
// A line, after " \t\r" is cut from both its ends:
//   empty, or starting with ";", "#" or "[" - a comment, passed over;
//   without "=" - said in the log, passed over;
//   `key = value` or `key += value` (a "+" at the key's end: a list's items added to it).
// The comment after a value, by the value's kind (comment_e):
//   anywhere - numbers, switches, hexadecimal names, colours, lists of names, sizes, choices: the first ";" or "#"
//              in the value starts the comment (`wings = 1;on` is 1);
//   after_space - plain text (a list of dll names): only a ";" or "#" after a space or a tab, the space after "="
//                 too (`k = #x` is empty, `k =#x` is "#x");
//   none - a path, a folder or a dll's name, which may hold them (`D:\Games #2\...`).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace edplugin_common
  {
  enum struct ini_line_e
    {
    empty,      ///< nothing but spaces
    comment,    ///< starting with ";", "#" or "["
    no_equals,  ///< words without "="
    setting     ///< `key = value` or `key += value`
    };

  struct ini_line_t
    {
    ini_line_e kind;
    std::string_view key;    ///< trimmed, the "+" of `+=` off
    std::string_view value;  ///< all after "=", its comment not cut yet (ini_value), its end trimmed
    bool adding;             ///< `key += value`
    };

  ///\brief a value's comment rule, by its kind (the header's comment)
  enum struct comment_e
    {
    anywhere,
    after_space,
    none
    };

  ///\brief what a plain text holds: words (a comment after a space cut off) or a path, a folder or a dll's name (no
  /// comment: the characters stay as written)
  enum struct value_e
    {
    plain,
    raw
    };

  [[nodiscard]]
  constexpr auto ini_trim(std::string_view s) noexcept -> std::string_view
    {
    while(not s.empty() and (s.front() == ' ' or s.front() == '\t' or s.front() == '\r'))
      s.remove_prefix(1);
    while(not s.empty() and (s.back() == ' ' or s.back() == '\t' or s.back() == '\r'))
      s.remove_suffix(1);
    return s;
    }

  ///\brief one line of the ini (no "\n" in it)
  [[nodiscard]]
  constexpr auto parse_ini_line(std::string_view line) noexcept -> ini_line_t
    {
    line = ini_trim(line);
    if(line.empty())
      return {ini_line_e::empty, {}, {}, false};
    if(line.front() == ';' or line.front() == '#' or line.front() == '[')
      return {ini_line_e::comment, {}, {}, false};
    std::size_t const eq{line.find('=')};
    if(eq == std::string_view::npos)
      return {ini_line_e::no_equals, {}, {}, false};
    std::string_view key{ini_trim(line.substr(0, eq))};
    bool const adding{not key.empty() and key.back() == '+'};
    if(adding)
      key = ini_trim(key.substr(0, key.size() - 1));
    return {ini_line_e::setting, key, line.substr(eq + 1), adding};
    }

  ///\brief the value without its comment (by `rule`), trimmed
  [[nodiscard]]
  constexpr auto ini_value(std::string_view value, comment_e rule) noexcept -> std::string_view
    {
    if(rule == comment_e::anywhere)
      {
      if(std::size_t const at{value.find_first_of(";#")}; at != std::string_view::npos)
        value = value.substr(0, at);
      }
    else if(rule == comment_e::after_space)
      {
      for(std::size_t i{1}; i < value.size(); ++i)
        if((value[i] == ';' or value[i] == '#') and (value[i - 1] == ' ' or value[i - 1] == '\t'))
          {
          value = value.substr(0, i);
          break;
          }
      }
    return ini_trim(value);
    }

  ///\brief the key a line names, also as a comment (`;key = 1`, `# key += A`): for the keys an ini already has; empty
  /// for a line of words
  [[nodiscard]]
  constexpr auto ini_named_key(std::string_view line) noexcept -> std::string_view
    {
    line = ini_trim(line);
    while(not line.empty() and (line.front() == ';' or line.front() == '#' or line.front() == ' ' or line.front() == '\t'))
      line.remove_prefix(1);
    std::size_t const eq{line.find('=')};
    if(eq == std::string_view::npos)
      return {};
    std::string_view key{ini_trim(line.substr(0, eq))};
    if(not key.empty() and key.back() == '+')
      key = ini_trim(key.substr(0, key.size() - 1));
    return key;
    }

  ///\brief the text without a UTF-8 byte order mark at its start
  [[nodiscard]]
  constexpr auto ini_without_bom(std::string_view text) noexcept -> std::string_view
    {
    if(text.starts_with("\xEF\xBB\xBF"))
      text.remove_prefix(3);
    return text;
    }

  ///\brief a file starting with UTF-16's byte order mark (either order): not read and not added to
  [[nodiscard]]
  constexpr auto ini_is_utf16(std::string_view text) noexcept -> bool
    {
    return text.starts_with("\xFF\xFE") or text.starts_with("\xFE\xFF");
    }

  ///\brief the line end lines added to the text take: its own ("\r\n" when it has one, else "\n" when it has one),
  /// "\r\n" for a text without any (a new file, a file of one line)
  [[nodiscard]]
  constexpr auto ini_line_end(std::string_view text) noexcept -> std::string_view
    {
    if(text.find("\r\n") != std::string_view::npos)
      return "\r\n";
    if(text.find('\n') != std::string_view::npos)
      return "\n";
    return "\r\n";
    }

  ///\brief a whole finite number as strtof takes it (`+10`, `1e3`, a dot for the decimals); none for anything after
  /// it (`0,45`), NaN or infinity
  [[nodiscard]]
  inline auto read_float(std::string_view value) -> std::optional<float>
    {
    std::string const text{value};
    char * end{};
    float const v{std::strtof(text.c_str(), &end)};
    if(text.empty() or end != text.c_str() + text.size() or not std::isfinite(v))
      return std::nullopt;
    return v;
    }

  ///\brief digits only, no more than fit 32 bits
  [[nodiscard]]
  constexpr auto read_uint32(std::string_view value) noexcept -> std::optional<std::uint32_t>
    {
    if(value.empty())
      return std::nullopt;
    std::uint64_t v{};
    for(char const c: value)
      {
      if(c < '0' or c > '9')
        return std::nullopt;
      v = v * 10u + static_cast<std::uint64_t>(c - '0');
      if(v > 0xFFFFFFFFull)
        return std::nullopt;
      }
    return static_cast<std::uint32_t>(v);
    }

  ///\brief 1, 0, true or false
  [[nodiscard]]
  constexpr auto read_bool(std::string_view value) noexcept -> std::optional<bool>
    {
    if(value == "1" or value == "true")
      return true;
    if(value == "0" or value == "false")
      return false;
    return std::nullopt;
    }

  ///\brief whether a hexadecimal name of 0 is taken (edworld: "0" asks it to find the shader) or refused
  enum struct hex_zero_e
    {
    refused,
    taken
    };

  ///\brief a hexadecimal name: an optional `+` (strtoull took it), an optional `0x`/`0X`, then 1 to 16 hex digits
  /// after any leading zeros; `-` refused (strtoull made a wrong name of it)
  [[nodiscard]]
  constexpr auto read_hex64(std::string_view value, hex_zero_e zero) noexcept -> std::optional<std::uint64_t>
    {
    if(value.starts_with('+'))
      value.remove_prefix(1);
    if(value.starts_with("0x") or value.starts_with("0X"))
      value.remove_prefix(2);
    if(value.empty())
      return std::nullopt;
    std::uint64_t v{};
    std::size_t significant{};
    for(char const c: value)
      {
      std::uint64_t d{};
      if(c >= '0' and c <= '9')
        d = static_cast<std::uint64_t>(c - '0');
      else if(c >= 'a' and c <= 'f')
        d = static_cast<std::uint64_t>(c - 'a' + 10);
      else if(c >= 'A' and c <= 'F')
        d = static_cast<std::uint64_t>(c - 'A' + 10);
      else
        return std::nullopt;
      if(significant != 0 or d != 0)
        ++significant;
      if(significant > 16)
        return std::nullopt;
      v = (v << 4) | d;
      }
    if(v == 0 and zero == hex_zero_e::refused)
      return std::nullopt;
    return v;
    }

  ///\brief a colour RRGGBB, as read_hex64 takes it, at most 0xFFFFFF (0 is black)
  [[nodiscard]]
  constexpr auto read_hex24(std::string_view value) noexcept -> std::optional<std::uint32_t>
    {
    std::optional<std::uint64_t> const v{read_hex64(value, hex_zero_e::taken)};
    if(not v or *v > 0xFFFFFFull)
      return std::nullopt;
    return static_cast<std::uint32_t>(*v);
    }

  ///\brief a list's items read: the names, in their order (a name twice stays twice), and the words that are none
  struct hex_list_t
    {
    std::vector<std::uint64_t> names;
    std::vector<std::string_view> bad;
    };

  ///\brief names as read_hex64 takes them, by commas, spaces or tabs (empty items passed over)
  [[nodiscard]]
  inline auto read_hex_list(std::string_view value, hex_zero_e zero) -> hex_list_t
    {
    hex_list_t list;
    std::size_t at{};
    while(at < value.size())
      {
      at = value.find_first_not_of(" ,\t", at);
      if(at == std::string_view::npos)
        break;
      std::size_t const end{value.find_first_of(" ,\t", at)};
      std::string_view const word{value.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at)};
      if(std::optional<std::uint64_t> const v{read_hex64(word, zero)})
        list.names.push_back(*v);
      else
        list.bad.push_back(word);
      if(end == std::string_view::npos)
        break;
      at = end;
      }
    return list;
    }

  ///\brief UTF-8 as UTF-16 code units; a byte that does not begin a whole UTF-8 sequence becomes U+FFFD
  [[nodiscard]]
  inline auto ini_utf16(std::string_view text) -> std::wstring
    {
    std::wstring out;
    out.reserve(text.size());
    std::size_t i{};
    while(i < text.size())
      {
      auto const b0{static_cast<unsigned char>(text[i])};
      std::size_t length{};
      std::uint32_t c{};
      std::uint32_t least{};
      if(b0 < 0x80u)
        {
        length = 1;
        c = b0;
        }
      else if(b0 >= 0xC2u and b0 < 0xE0u)
        {
        length = 2;
        c = b0 & 0x1Fu;
        least = 0x80u;
        }
      else if(b0 >= 0xE0u and b0 < 0xF0u)
        {
        length = 3;
        c = b0 & 0x0Fu;
        least = 0x800u;
        }
      else if(b0 >= 0xF0u and b0 < 0xF5u)
        {
        length = 4;
        c = b0 & 0x07u;
        least = 0x10000u;
        }
      bool whole{length != 0 and i + length <= text.size()};
      for(std::size_t k{1}; whole and k != length; ++k)
        {
        auto const b{static_cast<unsigned char>(text[i + k])};
        whole = (b & 0xC0u) == 0x80u;
        c = (c << 6) | (b & 0x3Fu);
        }
      if(not whole or c < least or c > 0x10FFFFu or (c >= 0xD800u and c < 0xE000u))
        {
        out += static_cast<wchar_t>(0xFFFD);
        ++i;
        continue;
        }
      if(c >= 0x10000u)
        {
        out += static_cast<wchar_t>(0xD800u + ((c - 0x10000u) >> 10));
        out += static_cast<wchar_t>(0xDC00u + ((c - 0x10000u) & 0x3FFu));
        }
      else
        out += static_cast<wchar_t>(c);
      i += length;
      }
    return out;
    }

  ///\brief UTF-16 code units as UTF-8; a surrogate without its pair becomes U+FFFD
  [[nodiscard]]
  inline auto ini_utf8(std::wstring_view text) -> std::string
    {
    std::string out;
    out.reserve(text.size());
    auto put = [&](std::uint32_t c)
      {
      if(c < 0x80u)
        out += static_cast<char>(c);
      else if(c < 0x800u)
        {
        out += static_cast<char>(0xC0u | (c >> 6));
        out += static_cast<char>(0x80u | (c & 0x3Fu));
        }
      else if(c < 0x10000u)
        {
        out += static_cast<char>(0xE0u | (c >> 12));
        out += static_cast<char>(0x80u | ((c >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (c & 0x3Fu));
        }
      else
        {
        out += static_cast<char>(0xF0u | (c >> 18));
        out += static_cast<char>(0x80u | ((c >> 12) & 0x3Fu));
        out += static_cast<char>(0x80u | ((c >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (c & 0x3Fu));
        }
      };
    for(std::size_t i{}; i < text.size(); ++i)
      {
      auto const u{static_cast<std::uint32_t>(text[i]) & 0xFFFFu};
      if(u >= 0xD800u and u < 0xDC00u and i + 1 < text.size())
        {
        auto const v{static_cast<std::uint32_t>(text[i + 1]) & 0xFFFFu};
        if(v >= 0xDC00u and v < 0xE000u)
          {
          put(0x10000u + ((u - 0xD800u) << 10) + (v - 0xDC00u));
          ++i;
          continue;
          }
        }
      put(u >= 0xD800u and u < 0xE000u ? 0xFFFDu : u);
      }
    return out;
    }
  }  // namespace edplugin_common
