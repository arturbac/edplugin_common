# Changelog

## Unreleased

- `ini/view.h` and `ini/keeper.h` (no Windows header): the settings thread's core without the thread, one code for
  every plugin. `ini_view_of` gives a plugin's table and `live3_settings_t` without its types; `ini_keeper_t` turns a
  look at the ini and a read of it into a publication and lines for the log (a file taken away, one that cannot be
  read, UTF-16, the read's notes), and freezes the settings after a fault in a change, after the last attempt or when
  a read throws. Checked natively with ASan, UBSan and TSan (`test/native/keeper_native.cc`) and alone.
- `tools/context_probe/`: `context_probe.exe` 7, for the module `hook` on Windows: on each device three plugins attach
  with the library's code (`hooks.cc`: A and B by the rule, C asking for a table of its own), hook `Draw`,
  `IASetPrimitiveTopology`, `ClearState` and `Flush` and count their calls; the calls through `ID3D11DeviceContext4`,
  with the multithread protection on and off and after `ClearState` and `Flush`, each step with the watches' classes,
  the runtime's entries followed and the protection told; the entries past the named methods counted by the module
  they lead to (`module_place` in `slots.h`: the driver store, the Windows folder, outside it); a `ResizeBuffers` and
  one `Present` more after the swap chain's four, the context's table the object points at and the one it came with
  both compared; the plugins' hooks again after the swap chain. `package.sh` makes `context_probe-7.zip` with the
  library's hook sources. A fault in the plugins' hooks ends only their step (said, no hooks on the devices after it);
  a context whose table holds an earlier device's plugin hooks gets none; each hook takes the run of the context it is
  called for; the process ends by `TerminateProcess` (the devices the hooks keep stay alive).
- `ini/live.h`: `live3_settings_t` beside `live_settings_t` (which stays as it is), for settings published on a
  thread other than the one that reads them: three slots, one exchange per side (wait-free, no allocation on the
  consumer), a start copy any thread may read (`start_fields`), `live_fields` for reads in many hooks (the render
  thread's slot, else the start copy and one line a session through the plugin's `live3_told_t`), and `live3_start`
  for a global atomic pointer to it. Checked natively with ASan, UBSan and TSan; `tools/test-native.sh` also runs a
  control under TSan (`test/native/live3_control.cc`) that has to report a data race. One consumer thread takes
  the slots for the whole session; an `acquire` on another thread is said once a session (`other_acquire_line`).
- The module `hook` (`include/edplugin_common/hook/`, `src/context.cc`, `src/own_table.cc`, `src/slot.cc`,
  `src/watch.cc`, `src/multithread.cc`) and `chain/system_d3d11.h` (`src/system_d3d11.cc`): a plugin's hooks on the
  game's immediate context kept when Windows' d3d11 rewrites its method table. `context_rule.h` picks a table of the
  plugin's own or in place (`context_pick`, its cases as `static_assert`s); `own_table.h` makes the table of its own
  (stubs through the runtime's entries, a mark another copy recognises it by); `slot.h` writes a list of slots in place,
  all or nothing; `context.h` attaches (`context_hooks_t`: `works(this)` false until the whole list is in, one try
  per plugin - a second attach is refused before anything changes, another context always passes); `watch.h`
  classes the watched entries and the object's table (ours, covered, taken, taken by the runtime, a plugin above,
  re-pointed) and counts calls for the census; `multithread.h` tells the runtime's
  multithread protection; `contract.h` the contract's version (1). `copy_readable(to, from, bytes)` in
  `memory/memory.h`. Checked natively with ASan, UBSan and TSan (the rule, the mark, the write order, the classes) and
  under wine (`test/context_test.cc`: a context of the test's own on a heap table, the runtime rewriting it, two copies,
  a plugin above with a table of its own, a list that cannot be written, an entry taken back, the protection turned on,
  a second device, a second attach refused).
- `dxgi/device_removed.h` (`src/device_removed.cc`): a lost device told once for the module in the log, the line
  flushed: `note_present_result(chain, hr)` after a Present (a failure reads the device's `GetDeviceRemovedReason`;
  `DXGI_ERROR_DEVICE_REMOVED`, `_RESET` or a reason other than `S_OK` is logged), `check_device_removed(device)` for a
  plugin without a Present, its caller at most once a second; the codes by name (`device_code_name`). `log_flush()`:
  the log's file, this thread's batch too, flushed to the disk, for a line that has to outlive a stop of the system.
- `edplugin_common_version`: the header also has `<macro>_COMMIT` (HEAD's full commit) and `<macro>_BUILD_TIME`
  (`SOURCE_DATE_EPOCH`, else HEAD's commit time, in UTC; `-dirty` as the version), and in a Windows build each
  target gets a version resource (VERSIONINFO): the version text, the target's names, numbers X,Y,Z,K (K the commits
  after the last tag), PRERELEASE and PRIVATEBUILD flags, and links reproducibly: the build's time as the time stamp
  (`/timestamp` from a response file version.cmake writes; `/Brepro` without a time) and `/pdbaltpath:%_PDB%`, so
  the same code and options give the same file in the same build directory; no clock of the build anywhere.
  `<macro>` and its text are as before. `test/version_test.sh` (in `tools/test.sh`, without wine) checks it, with
  the resource of the build's `test_exports.dll`; `test/reproducible_test.sh` builds a dll twice and compares the
  files and their time stamp.
- event_report: `--around <UTC date or time>` (`2026-11-19` the whole day, `2026-11-19T16:06` with `--minutes` either
  side, 60 unless given; up to 16) shows only the entries, saved reports and dumps of those times, for a helper who
  knows when a session ended (before, only the newest 20 of each kind over 30 days); without it the report is as
  before. A word not understood ends with 1 and the usage.
- `memory/pass_modules.h` (`src/pass_modules.cc`): the list of modules passed over when a plugin tells who called it
  (`default_pass_modules`, `bdhkm64.dll`), its reading (`next_pass_name`) and the comparison of a dll's name with it
  (`passed_over`), as edworld's `creator_pass` reads and compares today, for edworld and edloader alike.
- The module `ini` (`include/edplugin_common/ini/`, `src/ini.cc`): a plugin's ini by its one table of settings,
  taken from edupscale, edwide_screen and edworld so the three read, write and watch it alike. `ini_text.h` (a line,
  the comment by the value's kind, the readers, byte order marks, line ends, UTF-8 and UTF-16), `ini_table.h` (the
  table, `read_ini_text` with one form of notes, withdrawn keys, `at_start_changes`, `ini_completion`,
  `ini_doc_rows_t`), `live.h` (`live_settings_t`: two slots, the generation in the slot, only the live rows copied at
  a publication) and `ini.h` (`complete_ini`, `read_ini_file`, `ini_watch_t`). Without Windows the first three are
  checked natively with ASan, UBSan and TSan; the files under wine.
- `append_to_file(path, data, bytes, mode)`: the modes `create_new` (a file not written whole taken away through its
  own handle) and `append_existing` (held unshared, written at its end, cut back to its length when not written
  whole); `create_or_append` by default, as before. On false, `GetLastError()` is the cause.
  `append_to_file_cut` for the library's test only.
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
