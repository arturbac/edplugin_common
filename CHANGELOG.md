# Changelog

## Unreleased

- The modules `core` (`exclusive_lock_t`, `shared_lock_t`, `once_flag_t`) and `text` (`wide`, `utf8`, `as_unsigned`,
  their `std::formatter`s, `try_format_into`), headers taken from the plugins. `utf8` ends on bytes that are not
  UTF-8: a piece goes back at most 3 continuation bytes (before, 240 or more of them in a row made an endless
  loop).
- Build steps as functions: `edplugin_common_version`, `edplugin_common_pin` (the clone against the plugin's
  `edplugin_common.commit` at every build; a warning, an error in a release build) and `edplugin_common_fxc`;
  `cmake/version.cmake` moved here from the plugins.
- Tests: `test/pin_test.sh` (without wine) and the modules' checks in `edplugin_common_test.exe`.
