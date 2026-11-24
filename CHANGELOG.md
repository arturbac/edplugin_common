# Changelog

## Unreleased

- event_report builds with the `kdev-*` presets too: its delay-load of `wevtapi.dll` goes to the linker through
  CMake's `LINKER:` prefix (before, clang's GNU-style driver took `/DELAYLOAD:wevtapi.dll` for an input file); the
  release build's import table as before (`wevtapi.dll` delay-loaded).
- `settings_doc`: the doc's comment says `tools/test.sh or tools/doc.sh checks it is up to date` (before, only
  `tools/test.sh`, though a plugin's `tools/doc.sh` makes the check); a plugin's `doc/settings.md` changes with the
  next pin.
- event_report: the result file is written by `write_result` (`tools/event_report/result_file.cc`), with the folder
  given; `event_report_test.exe` checks its names with three runs in one second (`-2`, `-3`, none written over) and
  a folder that is not there; `test/event_report_test.sh` checks both runs' files whole (before, only the first in
  the folder's order, which was either run's).
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
  `map_file_ro`), `memory` (`readable`, `executable`, `module_of`, `section_of`,
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
- `tools/context_probe/`: `context_probe.exe` 6, after the probe edworld used on other Windows: every hardware adapter
  (the default and the high performance one named) and the default device, the slots the runtime rewrites, each table
  entry outside the system d3d11 with its method's name and the module its trampoline leads to, the entries in the
  system d3d11 that start with a jump out of it, the process's modules outside the Windows folder; then a swap chain on
  a window never shown and a few `Present` without drawing, after each step the swap chain's table, the device's and the
  context's slots changed since before it and the modules that came, after the last `Present` the device's and the
  context's tables told again; each run's result in a file of its own, named by the run's time; tables read as copies
  under `__try`; `context_probe_test.exe` in `tools/test.sh`, the decoding of jumps and the comparison of reads in
  `test/native`; `tools/context_probe/package.sh` for the zip a player gets (Release only, from a committed tree).
- The module `guard`: `run_guarded` / `run_guarded_call` (a body under a fault handler, C++ exceptions caught too,
  the stack's guard page set again after an overflow, the library's locks the body still held let go after a fault,
  the thread's log batch ended after a fault or a C++ exception), `fault_line`, `guard_t` (a part with a limit of
  faults, its own handler, off for the session after the limit, a stack overflow or a fault in another module),
  `run_guarded_thread`, and `fault_action`, which every filter of the library now uses: the codes that are no fault (a thread's name, debug output, a breakpoint or single step under a
  debugger, UBSan's trap in its test build) go on to the next handler instead of being taken for a fault. `src/guard.cc`
  is built without LTO; a target built with `INTERPROCEDURAL_OPTIMIZATION` is a warning at configure.
- `core/lock.h`: `exclusive_lock_t` and `shared_lock_t` note each held lock on a short stack of the thread, for the
  guard; a destructor takes its own lock's entry off, wherever it stands.
- `log_break()` and `log_is_broken()`: the log off for the session after a fault in its own write.
- `memory/pe.h`: `pe_image` / `pe_image_t` (a loaded module's image read as data, every read kept to a bound and
  copied out), `for_each_export`, `export_address`, `for_each_import`; `memory/pe_range.h`, its ranges, also in
  `test/native`. `section_of` (now under the guard) and `first_export_with_prefix` read through it. `pe_headers`
  removed (`pe_image` instead).
- `tools/event_report/`: `event_report.exe` 1, the game's events from Windows' logs and Windows Error Reporting's saved
  reports for a player to read and send back: crashes, hangs, graphics driver resets and errors, restarts and blue
  screens, the files loaded into the game when it crashed, crash dumps' names; only kept fields, no path, names and ids
  replaced; each run's result in a file of its own, named by the run's time; `event_report_test.exe` and
  `test/event_report_test.sh` in `tools/test.sh`, the part without Windows in `test/native`;
  `tools/event_report/package.sh`.

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
