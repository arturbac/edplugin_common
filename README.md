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
- `cmake/clang-cl-msvc-sdk.cmake` - the toolchain: a cross build for 64-bit Windows on Linux with clang-cl and LLVM's
  tools against MSVC's and the Windows SDK's headers and libraries, from the directory `MSVC_ROOT` (cache or
  environment variable) in clang's `/winsysroot` layout (`VC/Tools/MSVC`, `Windows Kits/10`), e.g. the one
  msvc-wine installs.
- `cmake/get_cpm.cmake` - CPM, which brings stralgo and small_vectors (pinned to their commits).
- `cmake/edplugin_common.cmake` - the build steps the plugins share, as functions (see below), with
  `cmake/version.cmake` and `cmake/pin.cmake` they run at every build.
- The target `edplugin_common::edplugin_common`: the headers above, compiled as C++23 with `UNICODE`, `_UNICODE`,
  `NOMINMAX` and `WIN32_LEAN_AND_MEAN`, linked with stralgo. The modules are headers only so far, so it is an
  INTERFACE target.
- `tools/wine-common.sh` - the common part of the plugins' `tools/test.sh` (wine prefix, run directory, a time
  limit on every wine run).

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
"${sourceDir}/edplugin_common/cmake/clang-cl-msvc-sdk.cmake"`. The library's tests build only when it is the
top-level project.

- `edplugin_common_version(<macro> <header> <target>...)`: the header `<build>/gen/<header>` with the macro defined
  to the plugin's version, forced into the C++ sources of each target (not into MASM sources): `vX.Y.Z` for a
  release (HEAD on its tag, no uncommitted changes), else `<the last tag>-<short hash>`, with `-dirty` for
  uncommitted changes; `dev` outside git.
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
  in the list. The output follows the layout of the export thunks of the
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
- Push order: the library's commit is on GitHub before a plugin whose `edplugin_common.commit` names it.
- After a test one wineserver stays 600 s for the next one; `wineserver -k` with the same `WINEPREFIX` ends it.

## Build and test

Requirements: CMake 3.29 or later, Ninja, clang-cl, lld-link, llvm-lib, llvm-rc, llvm-mt, llvm-ml64, MSVC and the
Windows SDK in `MSVC_ROOT`, git (the pin check at every build), wine for the tests.

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
d3d11 or dxgi) under wine, each in a new directory under the scratch directory, and prints `PASSED` or `FAILED`.
One wine run is limited to `WINE_COMMON_TIMEOUT` seconds (600 unless set). The CMake option `EDPLUGIN_COMMON_TESTS` (ON by default) turns the
test program off.
