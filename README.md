# edplugin_common

A static C++23 library shared by the Elite Dangerous d3d11 plugins: edloader, edworld, edupscale, edwide_screen and
edshader-catch. Every plugin's dll links its own copy of the code and has its own state. The library does not depend
on any particular loader.

License: MIT (see `LICENSE`).

## What it holds

- `include/edplugin_common/core/lock.h` - `exclusive_lock_t`, `shared_lock_t`: an SRW lock held for a scope.
- `include/edplugin_common/core/once_flag.h` - `once_flag_t`: `first()` is true for the first caller only (until
  `reset()`), also from several threads; constant-initialised, so a global one does no work when the dll is loaded.
- `include/edplugin_common/text/format.h` - for `std::format`: `wide()` (a wide string in a narrow line, as UTF-8),
  `utf8()` (a UTF-8 string in a wide text, as UTF-16; bytes that are not UTF-8 become U+FFFD), `as_unsigned()` (a
  signed value or an enumeration as its bits, for hexadecimal fields), and `try_format_into(buf, [size,] fmt, ...)`:
  `std::format` into a fixed buffer, cut at its end and NUL-terminated, never thrown; the characters written, or no
  value when the text could not be made (the buffer then empty), so the caller logs it. A plugin keeps its own
  `format_into` over it with its log; the library's function has another name, so a plugin's `format_into` called
  unqualified with an argument of the library's types does not find it by argument-dependent lookup.
- `include/edplugin_common/log/log.h` (code in `src/log.cc`) - a plugin's log, one per module:
  - `log_open(dir, name)`: `<dir>\<name>.log` for this game session. A non-empty file there from the last session
    is first set aside as `<dir>\<name>\<name>.<UTC of its last write>.log` (the directory made when missing);
    two sessions whose last writes fall in the same second get one name, and the second is not set aside. When
    the file cannot be set aside (the name taken, a file where the directory would be, a directory's path over 247
    characters or the moved file's over 259, or another error), the session appends to it and logs why, with the
    error's code. A second call while the log is open does nothing and is true; else false, said by
    `OutputDebugString`, for an empty directory or name, a log's path over 259 characters or a file that cannot be
    opened. Older sessions are not removed.
  - `log_line(fmt, ...)` / `log_vline(fmt, args, tap)`: one line, a UTC time stamp with milliseconds, the text and a
    line end, cut to 2 KB and written with Windows' own calls under an SRW lock; a text that cannot be made is cut
    where it failed and says so; never thrown. The optional tap (`line_tap_t`) gets each line's text (without stamp
    and line end) once, before it is written, outside the log's lock, also in a batch and when the log is not open.
  - `log_batch_begin()` / `log_batch_end()`: one thread's lines gathered (16 KB) and written at once, at the end or
    when the batch is full; another thread's lines are written at once meanwhile; batches do not nest (the first
    end ends one). The thread ends its batch on every way out, a fault too (its fault handler calls
    `log_batch_end`): a batch never ended keeps its lines and stays that thread's for good.
  - `log_line_to(path, fmt, ...)`: one such line appended to another file (opened, written, closed).
  - `log_is_open()`; `log_close()` for tests.

  Every global of the log is constant-initialised and trivially destructible, so the library adds no constructor or
  destructor to a dll. Nothing of it may be called from `DllMain`, and the log is not closed at
  `DLL_PROCESS_DETACH` (the game's threads are killed at its exit).
- `include/edplugin_common/log/state_log.h` - `state_log_t<value_t>`: the last value of a state (an integer or an
  enumeration, lock-free); `changed(now)` is true when it differs from the last one, so `if(s.changed(v))
  log_line(...)` logs once per change of state, not once per frame. The first value, given to the constructor, is
  the caller's "unknown".
- `include/edplugin_common/paths/paths.h` (code in `src/paths.cc`) - `environment(name)` (a value of any length,
  empty when not set), `module_path(module)` (a long path too; `nullptr` is the exe), `directory_of(path)` and
  `file_name_of(path)` (around the last `\` or `/`, copies), the names of the variables edloader sets for its plugins
  (`edloader_config_dir_variable`, `edloader_log_dir_variable`) and `plugin_dir(variable, self)` (the variable's
  folder, without it the plugin's dll's own), and `game_build_key()`: the game's build as the plugins name folders by
  it, `<the exe's size, low 32 bits>-<its last write, 16 hex digits>`, told once in the process, empty when the exe
  cannot be read (a long path too).
- `include/edplugin_common/file/file.h` (code in `src/file.cc`) - `read_text(path)`: the file whole, empty when there
  is no such file, no value when it is there but cannot be read (so a caller never takes it for an empty one);
  `write_time(path)` (0 without the file); `write_whole_file(path, data, bytes)`: made anew, a file not written whole
  removed, a failure one line in the module's log; `append_to_file(path, data, bytes)`; `map_file_rw(path, bytes,
  tag)`: a file shared with other processes (made when missing, its directory not; grown to `bytes`), a failure one
  line `<tag>: cannot open|map|view ...`; `map_file_ro(path, least, most)`: another process's file, none when missing
  or smaller than `least`, silent. The views of both stay for the process's life.
- `include/edplugin_common/memory/memory.h` (code in `src/memory.cc`) - `readable(p, bytes)` (committed, read access,
  no guard page, the whole range in one region), `executable(p)`, `module_of(p)` (the reference count unchanged),
  `pe_headers(module)` (a loaded module's PE32+ headers, each read checked), `section_of(module, p, name)`, and
  `first_export_with_prefix(module, prefix, name, fault)`: the first export by such a name, read from the image's
  export directory with every offset checked against its size, under a fault handler (none, found, malformed, or a
  fault with its code).
- `include/edplugin_common/shader/shader.h` - the names of shaders, without a Windows header: `fnv1a64(data, bytes
  [, h])` (FNV-1a 64 from the plugins' basis 1469598103934665603 - a digit short of FNV's published one, kept since
  every name made so far uses it; `h` goes on from a hash so far), `fnv_3dmigoto` (3DMigoto's FNV-1 from 0) and
  `migoto_name_text` (`3dmgt_` and 16 hex digits). `include/edplugin_common/shader/names_of.h` (code in
  `src/names_of.cc`): `names_of(code, bytes)`, both names under a fault handler, `read` false when the bytes could not
  be read.
- `include/edplugin_common/dxgi/format.h` - `is_integer(format)`: a UINT or SINT format.
- Fault handlers: a `__try` catches a fault only of what it calls (clang-cl without `/EHa` does not catch a fault of an
  instruction in the `__try` body itself), so the reads under one are in a function of their own (`names_of`,
  `first_export_with_prefix`), with no object to unwind in the function with the handler, and its filter always
  handles the fault (`EXCEPTION_EXECUTE_HANDLER`): a fault let through a `noexcept` function ends the process.
- `include/edplugin_common/settings/settings_doc.h` (code in `src/settings_doc.cc`) - a plugin's `doc/settings.md`
  from its own table of settings, one template for every plugin:
  - the plugin gives each setting as text: `setting_row_t` (name, default, group as an index, read at the start or
    while the game runs, range, what it does), and its title, intro and groups in `settings_doc_t`;
  - `settings_doc(doc, rows)`: the doc, the template byte for byte in the header's comment (by group, the rows read
    while the game runs first, a table each, `None.` for none); `|`, line ends and `<`, `>` outside code spans
    escaped; an error for a group out of range, a name twice or an empty name;
  - `write_settings_doc(variable, doc, rows)`: written to the file the environment variable names (nothing
    without it); the status, the error, the system's error code and the path come back for the plugin's log.
  Neither throws. A plugin's `tools/test.sh` sets the variable and compares the file with its `doc/settings.md`.
- `cmake/clang-cl-msvc-sdk.cmake` - the toolchain: a cross build for 64-bit Windows on Linux with clang-cl and LLVM's
  tools against MSVC's and the Windows SDK's headers and libraries, from the directory `MSVC_ROOT` (cache or
  environment variable) in clang's `/winsysroot` layout (`VC/Tools/MSVC`, `Windows Kits/10`), e.g. the one
  msvc-wine installs.
- `cmake/clang-msvc-sdk.cmake` - the same cross build with clang's GNU-style driver (`clang++
  --target=x86_64-pc-windows-msvc`, lld, llvm-ar) instead of clang-cl: the MSVC ABI and runtime as there, `MSVC_ROOT`
  by `-Xmicrosoft-windows-sys-root` (the driver's `/winsysroot`), `-std=c++23`. For an IDE whose code model
  is libclang reading `compile_commands.json` (KDevelop), which takes GNU-style options only; the `kdev-*` presets use
  it, the other presets and every release clang-cl.
- `cmake/get_cpm.cmake` - CPM, which brings stralgo and small_vectors (pinned to their commits).
- `cmake/edplugin_common.cmake` - the build steps the plugins share, as functions (see below), with
  `cmake/version.cmake` and `cmake/pin.cmake` they run at every build.
- The target `edplugin_common::edplugin_common`: a STATIC library (the code of log, paths, file, memory, shader
  names and settings_doc) with the headers above, compiled as C++23 with `UNICODE`, `_UNICODE`, `NOMINMAX` and `WIN32_LEAN_AND_MEAN` (PUBLIC, so the plugin's code too), linked
  with stralgo; built with the MSVC runtime the plugin sets.
- `tools/wine-common.sh` - the common part of the plugins' `tools/test.sh` (wine prefix, run directory, a time
  limit on every wine run).
- `tools/context_probe/` - `context_probe.exe`, a diagnosis run by hand on Windows, without the game (a player's
  computer, a VM): on each hardware adapter, where the D3D11 runtime keeps the method tables of a device and of its
  immediate context, which of the context's slots the runtime rewrites (multithread protection, `ClearState`), which
  entries lie outside the system d3d11 and where their trampolines lead (`module+offset`), and the process's modules
  outside the system folder (what was injected into it). The lines go to `context_probe.txt` beside it; a player's
  instructions in `README.txt` there. The decoding of jumps and the methods' names by slot (`jump.h`, `slots.h`)
  need no Windows header. Built at the top level with `EDPLUGIN_COMMON_CONTEXT_PROBE` (ON by default).

## Use in a plugin

The plugin keeps a clone of the library in its own directory, ignored by its git (`.gitignore`), on the commit the
plugin pins in its file `edplugin_common.commit` (a full commit hash):

```sh
git clone https://github.com/arturbac/edplugin_common.git edplugin_common
git -C edplugin_common checkout $(cat edplugin_common.commit)
```

and in its `CMakeLists.txt`, before `project()`, a check that the clone is there (the toolchain is in it), then:

```cmake
add_subdirectory(edplugin_common)
target_link_libraries(<plugin> PRIVATE edplugin_common::edplugin_common)
edplugin_common_version(<PLUGIN>_VERSION <plugin>_version.h <plugin>)
edplugin_common_pin(<plugin>)
```

The plugin's presets take the toolchain from the clone: `"toolchainFile":
"${sourceDir}/edplugin_common/cmake/clang-cl-msvc-sdk.cmake"`, and for the IDE's `kdev-*` presets
`.../clang-msvc-sdk.cmake`. Options in MSVC's form stay out of the plugin's own CMake: warnings by
`edplugin_common_warnings`, linker options with CMake's `LINKER:` prefix (`LINKER:/DEBUG:FULL`, `LINKER:/EXPORT:...`),
which both drivers take. The library's tests build only when it is the top-level project.

- `edplugin_common_version(<macro> <header> <target>...)`: the header `<build>/gen/<header>` with the macro defined
  to the plugin's version, forced into the C++ sources of each target (not into MASM sources): `vX.Y.Z` for a
  release (HEAD on its tag, no uncommitted changes), else `<the last tag>-<short hash>`, with `-dirty` for
  uncommitted changes; `dev` outside git.
- `edplugin_common_warnings(<target>...)`: the warning level of the C++ sources of each target: `/W4` with clang-cl,
  the same `-Wall -Wextra` with the GNU-style driver.
- `edplugin_common_pin(<target>...)`: at every build, the clone against `edplugin_common.commit`, and the header
  `<build>/gen/edplugin_common_commit.h` with `EDPLUGIN_COMMON_COMMIT`: the clone's short hash, `-dirty` for
  uncommitted changes in it, ` (pinned <short hash>)` when it is not on the pinned commit, `unknown` when there is
  no `edplugin_common.commit` or git cannot read the clone. A plugin logs it at its start in a line of its own.
  Another commit, changes in the clone, a pinned commit missing from the clone (fetch it), no pin file or no git are
  a warning in the build's output; in a release build of the plugin (HEAD on a tag `vX.Y.Z`, a pre-release tag
  `vX.Y.Z-beta.N` too, no uncommitted changes) they are an error. git's own message is shown: a clone owned by
  another account may need git's `safe.directory`.
- `edplugin_common_fxc(<out> <profile> <entry> <variable> <source> [fxc options...])`: a header with the shader
  compiled by the Windows SDK's `fxc.exe` under wine; wine and fxc are looked for at the first call (cache
  `EDPLUGIN_COMMON_WINE`, `EDPLUGIN_COMMON_FXC`: the newest fxc in `MSVC_ROOT` unless set).
- `edplugin_common_proxy_exports(<tag> <list> [WRAP <name>...])`: the exports of a dll that stands in for a system dll
  (a `d3d11.dll` proxy: tag `d3d11`), made at configure time from `<list>`, a text file relative to the calling
  directory: one export name a line, `#` starts a comment anywhere in a line, spaces and empty lines are ignored. In
  `<build>/gen`:
  - `<tag>_proxy.def`: each name not in `WRAP` exported as `<tag>_proxy_thunk_<i>`, each `WRAP` name as
    `<tag>_proxy_impl_<name>`; no ordinals (the linker numbers the names in alphabetical order).
  - `<tag>_proxy_thunks.asm` (MASM): the slot table `<tag>_proxy_real_procs`, one pointer for each name not in `WRAP`;
    `<tag>_proxy_thunk_<i>`, one indirect `jmp` through slot `i` (no prologue: the caller's frame and arguments reach
    the target unchanged, whatever its signature), where `i` counts the names not in `WRAP` in the list's order; and
    `<tag>_proxy_unresolved`, which returns 0.
  - `<tag>_proxy.h`: `<tag>_proxy_names` (the names of the slots, in their order), `<tag>_proxy_count`, and the C
    declarations of `<tag>_proxy_real_procs` and `<tag>_proxy_unresolved`.

  The plugin adds `${<tag>_PROXY_ASM}` and `${<tag>_PROXY_DEF}` to its dll's sources (`<tag>_PROXY_HEADER` names the
  header; the `gen` directory is on the include path through `edplugin_common_pin`), fills every slot before the first
  call (`GetProcAddress` on the next module by the name in `<tag>_proxy_names`, `<tag>_proxy_unresolved` where it has
  none) and declares and defines `extern "C"` `<tag>_proxy_impl_<name>` with that function's signature for each `WRAP`
  name. A file is rewritten only when its text changes, so an unchanged list rebuilds nothing; a change of the list,
  or a deleted output, configures again. Errors, with the list's line where there is one, and nothing written: a tag
  or name that is not a C identifier (decorated names with `@` or `?` are not supported; a `.def` keyword such as
  `DATA`, `PRIVATE` or `NONAME` is not refused and does not belong in the list), a name twice, a `WRAP` name
  twice or not in the list, a list without names or with every name in `WRAP`, `;`, `[`, `]` or a byte outside ASCII
  in the list outside comments (comments are taken out first, so they may hold any text). The output follows the
  layout of the export thunks of the
  [EDVR unofficial patch](https://github.com/characterecho-sean/edvr-unofficial-patch) (MIT); no code of it is here.
- The plugin sets the MSVC runtime (`CMAKE_MSVC_RUNTIME_LIBRARY`, static `/MT`) and the debug information format
  before `add_subdirectory`; the library sets them itself only as the top-level project.
- CPM comes with the library (`cpmaddpackage` is there after `add_subdirectory`); stralgo and small_vectors are
  pinned in the library only. CPM tells packages apart by name only: the first `cpmaddpackage` of a package wins, a
  later one with another `GIT_TAG` is silently ignored. Without the network: as in "Build and test" below.
- A plugin's `tools/test.sh` sources `edplugin_common/tools/wine-common.sh`; its functions and their exit codes are
  described at the top of that file. Without the clone the plugin's test stops with the clone command.
- To another pinned commit: `git -C edplugin_common fetch origin` (from where the clone came), then
  `git -C edplugin_common checkout $(cat edplugin_common.commit)`. A build directory made before the toolchain moved
  into the clone keeps the old path: configure it again with `--fresh`.
- Push order: the library's commit is on GitHub before a plugin whose `edplugin_common.commit` names it; a release's
  tag `vX.Y.Z` is pushed by itself (a push of the branch does not take it).
- After a test one wineserver stays 600 s for the next one; `wineserver -k` with the same `WINEPREFIX` ends it.

## Versions

The version is in `project()` in `CMakeLists.txt`; a release is an annotated tag `vX.Y.Z` on the release's commit,
with its notes in `CHANGELOG.md`. A plugin pins a full commit (`edplugin_common.commit`), not a version: the tag only
names the commit, and the pin check compares hashes. A published tag is not moved; a correction is a new version.
The numbers count the library's releases and are not semantic versioning: what a plugin builds with is its pinned
commit, so a change of the API can come in a 1.x release, and `CHANGELOG.md` always describes it.

## Build and test

Requirements: CMake 3.29 or later, Ninja, clang-cl (the `kdev-*` presets: clang, clang++, llvm-ar, llvm-ranlib),
lld-link, llvm-lib, llvm-rc, llvm-mt, llvm-ml64, MSVC and the Windows SDK in `MSVC_ROOT`, git (the pin check at every
build), wine for the tests.

```sh
export MSVC_ROOT=<MSVC and Windows SDK directory>
cmake --preset edplugin_common-release
cmake --build build/edplugin_common-release
EDPLUGIN_COMMON_WINEPREFIX=<a wine prefix> tools/test.sh build/edplugin_common-release <scratch directory>
```

Without the network: `CPM_SOURCE_CACHE=<a filled CPM cache>` and `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` at configure.

`tools/test.sh` runs `test/pin_test.sh` (`cmake/pin.cmake` on a library and a plugin repository it makes, with
git and without wine), `test/proxy_exports_test.sh` (`edplugin_common_proxy_exports` by `cmake -P` on lists it makes,
without wine; the expected files in `test/proxy_exports/`) and `edplugin_common_test.exe` (a plain program, without
d3d11 or dxgi; with the log's checks, among them 4 threads writing batches at once) and, when built,
`context_probe_test.exe` (the following of entries through trampolines it writes itself, without d3d11) under wine,
each in a new directory under the scratch directory, and prints `PASSED` or `FAILED`.
One wine run is limited to `WINE_COMMON_TIMEOUT` seconds (600 unless set). The CMake option
`EDPLUGIN_COMMON_TESTS` (ON by default) turns the test program off.

Sanitizers, in test builds only (a player gets the release build, without them):

- `EDPLUGIN_COMMON_UBSAN_TRAP=ON` (the preset `edplugin_common-ubsan`): UndefinedBehaviorSanitizer in trap mode, no
  runtime, for the library's and every linking target's C++ sources (a plugin sets the option at its configure);
  they also get the definition `EDPLUGIN_COMMON_UBSAN_TRAP=1`. Undefined behaviour ends the program with
  `0xC000001D` (an illegal instruction); under a fault handler (`names_of`, `first_export_with_prefix`) it comes back
  as a fault with that code. `tools/test.sh` runs the build as any other.
- Not for this target under wine: AddressSanitizer (the runtime MSVC ships stops at the start under wine), UBSan
  with its runtime (MSVC's does not link without AddressSanitizer's), ThreadSanitizer (clang-cl has none for
  Windows).
- `tools/test-native.sh <scratch directory>`: the headers without a Windows header (`test/native`: `shader/shader.h`,
  `tools/context_probe/jump.h` and `slots.h`)
  built natively with clang++ and its compiler-rt sanitizers, with `-fsanitize=address,undefined` and with
  `-fsanitize=thread`, and run; no wine and no MSVC. Each configure, build and run is limited to `NATIVE_TIMEOUT`
  seconds (600 unless set); a run stopped by the limit is said with its exit code.
