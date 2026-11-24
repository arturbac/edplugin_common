// edplugin_common - paths: the environment, a module's path and directory, the folders edloader hands its plugins,
// the game's build.
#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace edplugin_common
  {
  ///\brief the environment variable edloader sets for its plugins: the folder of their settings
  inline constexpr wchar_t edloader_config_dir_variable[]{L"EDLOADER_CONFIG_DIR"};
  ///\brief the environment variable edloader sets for its plugins: the folder of their logs
  inline constexpr wchar_t edloader_log_dir_variable[]{L"EDLOADER_LOG_DIR"};

  ///\brief an environment variable's value, of any length; empty when it is not set, empty, or cannot be read
  [[nodiscard]]
  auto environment(wchar_t const * name) noexcept -> std::wstring;

  ///\brief the full path of a module (nullptr: the process's exe), a long one too; empty when it cannot be told
  [[nodiscard]]
  auto module_path(HMODULE module) noexcept -> std::wstring;

  ///\brief the part of `path` before its last `\` or `/`; empty without one
  [[nodiscard]]
  auto directory_of(std::wstring const & path) noexcept -> std::wstring;

  ///\brief the part of `path` after its last `\` or `/`; the whole without one
  [[nodiscard]]
  auto file_name_of(std::wstring const & path) noexcept -> std::wstring;

  ///\brief a folder edloader hands its plugins (`variable`: edloader_config_dir_variable, edloader_log_dir_variable);
  /// without edloader (the variable not set) the directory of the plugin's own dll `self`
  [[nodiscard]]
  auto plugin_dir(wchar_t const * variable, HMODULE self) noexcept -> std::wstring;

  ///\brief the game's build as the plugins name their folders by it: the exe's size (its low 32 bits) and last write,
  /// `<size>-<high 8 hex digits><low 8 hex digits>`. Told once in the process; empty when the exe cannot be read
  [[nodiscard]]
  auto game_build_key() noexcept -> std::string_view;
  }  // namespace edplugin_common
