// edplugin_common - the modules passed over when a plugin tells who called it: a program that puts itself in the
// game's own calls (an antivirus's hooks), so a call seems to come from it. A list of dll file names by commas. Its
// reading has no Windows header (checked natively); the comparison of a name is Windows' (src/pass_modules.cc).
#pragma once

#include <string_view>

namespace edplugin_common
  {
  ///\brief the list by default: Bitdefender's hooks (`bdhkm64.dll`, seen 2026-11-17 in a player's log)
  inline constexpr std::wstring_view default_pass_modules{L"bdhkm64.dll"};

  ///\brief the next name of a list of dll file names by commas, without the spaces and tabs around it; `rest` is
  /// left after its comma (empty after the last name). A name left empty between two commas is returned empty
  [[nodiscard]]
  constexpr auto next_pass_name(std::wstring_view & rest) noexcept -> std::wstring_view
    {
    auto const comma{rest.find(L',')};
    std::wstring_view name{rest.substr(0, comma)};
    rest = comma == std::wstring_view::npos ? std::wstring_view{} : rest.substr(comma + 1);
    while(not name.empty() and (name.front() == L' ' or name.front() == L'\t'))
      name.remove_prefix(1);
    while(not name.empty() and (name.back() == L' ' or name.back() == L'\t'))
      name.remove_suffix(1);
    return name;
    }

  ///\brief whether `file` (a dll's file name, without its folder) is one of `list`'s names, without regard to case
  /// (Windows' ordinal comparison); an empty name of the list matches none
  [[nodiscard]]
  auto passed_over(std::wstring_view file, std::wstring_view list) noexcept -> bool;
  }  // namespace edplugin_common
