# Changelog

## Unreleased

- `cmake/clang-msvc-sdk.cmake`: the cross build with clang's GNU-style driver targeting the MSVC ABI, for an IDE's
  code model (libclang reads GNU-style options only); the `kdev-*` presets use it, the other presets clang-cl as
  before. The library's check of the compiler takes either driver.
- `edplugin_common_warnings(<target>...)`: `/W4` with clang-cl, `-Wall -Wextra` with the GNU-style driver.
  `edplugin_common_version` forces its header in by `/FI` or `-include`, as the driver takes it.
- The module `log`: `log_open` (one file per session, the last session's file set aside in `<dir>\<name>\`),
  `log_line` / `log_vline` with a tap, batches, `log_line_to`, `log_is_open`, `log_close`; taken from edworld's log,
  with its batch corrected (a batch was given up before its lines were written, so another thread's lines could be
  lost or written twice, and a full batch was given up and taken again) and the handle an atomic read again under
  the lock. `state_log_t`: a state whose changes are logged once each.
- The target is a STATIC library (`src/log.cc`), its settings PUBLIC; the plugin's MSVC runtime is its runtime.
- `edplugin_common_proxy_exports`: comments are taken out before the list is checked, so `;`, `[`, `]` and bytes
  outside ASCII may stand in them.

## 1.0.0

- First release.
- The modules `core` (`exclusive_lock_t`, `shared_lock_t`, `once_flag_t`) and `text` (`wide`, `utf8`, `as_unsigned`,
  their `std::formatter`s, `try_format_into`), headers taken from the plugins. `utf8` ends on bytes that are not
  UTF-8: a piece goes back at most 3 continuation bytes (before, 240 or more of them in a row made an endless
  loop).
- Build steps as functions: `edplugin_common_version`, `edplugin_common_pin` (the clone against the plugin's
  `edplugin_common.commit` at every build; a warning, an error in a release build) and `edplugin_common_fxc`;
  `cmake/version.cmake` moved here from the plugins.
- Tests: `test/pin_test.sh` (without wine) and the modules' checks in `edplugin_common_test.exe`.
- `edplugin_common_proxy_exports`: the exports of a proxy dll (`.def`, MASM thunks, a header with the slots' names)
  from a list of names, at configure time in CMake alone; test `test/proxy_exports_test.sh` (without wine).
