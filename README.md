# edplugin_common

A static C++23 library shared by the Elite Dangerous d3d11 plugins: edloader, edworld, edupscale, edwide_screen and
edshader-catch. Every plugin's dll links its own copy of the code and has its own state. The library does not depend
on any particular loader.

License: MIT (see `LICENSE`).

## What it holds

The repository holds the build setup the plugins share and the test harness:

- `cmake/clang-cl-msvc-sdk.cmake` - the toolchain: a cross build for 64-bit Windows on Linux with clang-cl and LLVM's
  tools against MSVC's and the Windows SDK's headers and libraries, from the directory `MSVC_ROOT` (cache or
  environment variable) in clang's `/winsysroot` layout (`VC/Tools/MSVC`, `Windows Kits/10`), e.g. the one
  msvc-wine installs.
- `cmake/get_cpm.cmake` - CPM, which brings stralgo and small_vectors (pinned to their commits).
- The target `edplugin_common::edplugin_common`: compiled as C++23 with `UNICODE`, `_UNICODE`, `NOMINMAX` and
  `WIN32_LEAN_AND_MEAN`, linked with stralgo. No module has sources yet, so for now it is an INTERFACE target that
  carries only these settings.
- `tools/wine-common.sh` - the common part of the plugins' `tools/test.sh` (wine prefix, run directory, a time
  limit on every wine run).

## Use in a plugin

The plugin keeps a clone of the library in its own directory, ignored by its git (`.gitignore`):

```sh
git clone https://github.com/arturbac/edplugin_common.git edplugin_common
```

and in its `CMakeLists.txt`:

```cmake
add_subdirectory(edplugin_common)
target_link_libraries(<plugin> PRIVATE edplugin_common::edplugin_common)
```

The library's tests build only when it is the top-level project.

- The plugin sets the MSVC runtime (`CMAKE_MSVC_RUNTIME_LIBRARY`, static `/MT`) and the debug information format
  before `add_subdirectory`; the library sets them itself only as the top-level project.
- CPM tells packages apart by name only: the first `cpmaddpackage` of stralgo or small_vectors wins, a later one
  with another `GIT_TAG` is silently ignored. A plugin that adds them too pins the same commits as the library.
- A plugin's `tools/test.sh` sources `edplugin_common/tools/wine-common.sh`; its functions and their exit codes are
  described at the top of that file.

## Build and test

Requirements: CMake 3.29 or later, Ninja, clang-cl, lld-link, llvm-lib, llvm-rc, llvm-mt, llvm-ml64, MSVC and the
Windows SDK in `MSVC_ROOT`, wine for the tests.

```sh
export MSVC_ROOT=<MSVC and Windows SDK directory>
cmake --preset edplugin_common-release
cmake --build build/edplugin_common-release
EDPLUGIN_COMMON_WINEPREFIX=<a wine prefix> tools/test.sh build/edplugin_common-release <scratch directory>
```

Without the network: `CPM_SOURCE_CACHE=<a filled CPM cache>` and `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` at configure.

`tools/test.sh` runs `edplugin_common_test.exe` (a plain program, without d3d11 or dxgi) under wine in a new
directory under the scratch directory and prints `PASSED` or `FAILED`. One wine run is limited to
`WINE_COMMON_TIMEOUT` seconds (600 unless set). The CMake option `EDPLUGIN_COMMON_TESTS` (ON by default) turns the
test program off.
