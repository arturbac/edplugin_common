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
- The modules `paths` (`environment`, `module_path`, `directory_of`, `file_name_of`, `plugin_dir`, edloader's variables'
  names, `game_build_key`), `file` (`read_text`, `write_time`, `write_whole_file`, `append_to_file`, `map_file_rw`,
  `map_file_ro`), `memory` (`readable`, `executable`, `module_of`, `pe_headers`, `section_of`,
  `first_export_with_prefix`), `shader` (`fnv1a64`, `fnv_3dmigoto`, `migoto_name_text`, `names_of`) and `dxgi`
  (`is_integer`), taken from the plugins. Changed on the way: `read_text` tells a file it could not read from a
  missing one; paths and environment values over 259 characters are whole; a mapping of 0 bytes is refused (to
  Windows it would be the whole file); `first_export_with_prefix` and `names_of`
  read in a function of their own under the fault handler, which catches a fault only there (the scan as it was in
  edupscale, reading in the `__try` body itself, ended the process on a fault in the test).
- Sanitizers in test builds: the option `EDPLUGIN_COMMON_UBSAN_TRAP` (preset `edplugin_common-ubsan`), UBSan in trap
  mode for the C++ sources of the library and the targets linking it, with the definition
  `EDPLUGIN_COMMON_UBSAN_TRAP=1`; `test/native` and `tools/test-native.sh`, the headers without a
  Windows header built natively with ASan and UBSan, and with TSan.
- The module `settings_doc`: `settings_doc` (a plugin's `doc/settings.md` from rows the plugin describes, one
  template for every plugin: edworld's, with `|`, line ends and `<`, `>` escaped) and `write_settings_doc` (to the
  file an environment variable names, the result for the plugin's log, `settings_doc_error_name` for an error);
  never thrown.
- `tools/context_probe/`: `context_probe.exe` 5, after the probe edworld used on other Windows: every hardware adapter
  (the default and the high performance one named), the slots the runtime rewrites, each table entry outside the
  system d3d11 with its method's name and the module its trampoline leads to, the process's modules outside the
  system folder; `context_probe_test.exe` in `tools/test.sh`, the decoding of jumps in `test/native`.

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
