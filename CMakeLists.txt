# edplugin_common: the static library shared by the Elite Dangerous d3d11 plugins (edloader, edworld, edupscale,
# edwide_screen, edshader-catch); every plugin's dll links its own copy, with its own state. A plugin takes it as a
# clone in its own directory (add_subdirectory(edplugin_common)); the library's tests build only when it is the
# top-level project. Built on Linux with clang-cl against MSVC's and the Windows SDK's headers and libraries
# (cmake/clang-cl-msvc-sdk.cmake, MSVC_ROOT); the tests run under wine (tools/test.sh).
cmake_minimum_required(VERSION 3.29)
project(edplugin_common VERSION 1.0.0 LANGUAGES CXX)

# clang-cl, or clang's GNU-style driver targeting the MSVC ABI (cmake/clang-msvc-sdk.cmake, for an IDE's code model)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
   OR NOT (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"))
  message(FATAL_ERROR "edplugin_common builds with clang-cl (or clang for the MSVC ABI): configure with a preset or "
                      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/clang-cl-msvc-sdk.cmake")
endif()

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
endif()

# stralgo and small_vectors it stands on, by CPM; the same packages, versions and commits as the plugins' and
# elite_help_tool's (a plugin that already added them gets the same targets). CPM_SOURCE_CACHE (a filled cache) and
# FETCHCONTENT_FULLY_DISCONNECTED=ON configure without the network
if(NOT COMMAND cpmaddpackage)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/get_cpm.cmake)
endif()
cpmaddpackage(
  NAME
  small_vectors
  GITHUB_REPOSITORY
  arturbac/small_vectors
  VERSION
  3.4.0
  # tag v3.4.0, pinned to its commit - a tag can be moved
  GIT_TAG
  768316b4050952988376cf216eddb93eaeeb697e
  EXCLUDE_FROM_ALL
  YES)
if(NOT TARGET small_vectors::small_vectors)
  add_library(small_vectors::small_vectors ALIAS small_vectors)
endif()
cpmaddpackage(
  NAME
  stralgo
  GITHUB_REPOSITORY
  arturbac/stralgo
  VERSION
  1.5.0
  # tag v1.5.0, pinned to its commit - a tag can be moved
  GIT_TAG
  b9ad0230efcb7f4450d4c5c2646ab7515c590f80
  OPTIONS
  "STRALGO_ENABLE_UNIT_TESTS OFF"
  EXCLUDE_FROM_ALL
  YES)
if(NOT TARGET stralgo::stralgo)
  add_library(stralgo::stralgo ALIAS stralgo)
endif()

# the build steps the plugins share: edplugin_common_version, edplugin_common_pin, edplugin_common_fxc,
# edplugin_common_proxy_exports
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/edplugin_common.cmake)

# the modules are headers only so far: an INTERFACE target carries them and what every plugin's code compiles with;
# with the first sources it becomes a STATIC library and these settings PUBLIC
add_library(edplugin_common INTERFACE)
add_library(edplugin_common::edplugin_common ALIAS edplugin_common)
target_include_directories(edplugin_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(edplugin_common INTERFACE cxx_std_23)
target_compile_definitions(edplugin_common INTERFACE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(edplugin_common INTERFACE stralgo::stralgo)

if(PROJECT_IS_TOP_LEVEL)
  option(EDPLUGIN_COMMON_TESTS "build the test programs (tools/test.sh runs them under wine)" ON)
  if(EDPLUGIN_COMMON_TESTS)
    # the library's own checks: a plain program, without d3d11 or dxgi
    # each header alone in a translation unit of its own: it includes what it needs
    add_executable(edplugin_common_test test/library_test.cc test/alone_lock.cc test/alone_once_flag.cc test/alone_format.cc)
    target_link_libraries(edplugin_common_test PRIVATE edplugin_common::edplugin_common)
    edplugin_common_warnings(edplugin_common_test)
  endif()
endif()
