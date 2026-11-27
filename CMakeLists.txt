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

# a STATIC library (the modules' code: log, paths, file, memory, pass_modules, pe, shader names, settings_doc, ini,
# guard, hook, chain) with the headers, and what every plugin's code compiles with (PUBLIC); its MSVC runtime is the
# plugin's, set before add_subdirectory
add_library(
  edplugin_common STATIC
  src/log.cc
  src/paths.cc
  src/file.cc
  src/memory.cc
  src/pass_modules.cc
  src/pe.cc
  src/names_of.cc
  src/settings_doc.cc
  src/ini.cc
  src/guard.cc
  src/device_removed.cc
  src/own_table.cc
  src/slot.cc
  src/watch.cc
  src/multithread.cc
  src/system_d3d11.cc
  src/context.cc)
# the guard's __try stays around a call of its own only outside LTO (include/edplugin_common/guard/guard.h): this file
# never takes part in it, whatever a plugin's build does
set_source_files_properties(src/guard.cc PROPERTIES COMPILE_OPTIONS "-fno-lto")
add_library(edplugin_common::edplugin_common ALIAS edplugin_common)
target_include_directories(edplugin_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(edplugin_common PUBLIC cxx_std_23)
target_compile_definitions(edplugin_common PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(edplugin_common PUBLIC stralgo::stralgo)
edplugin_common_warnings(edplugin_common)
# a test build, never one for players: UndefinedBehaviorSanitizer in trap mode, with no runtime (MSVC's does not link
# without AddressSanitizer's, which stops at the start under wine), for the C++ sources of the library and of every
# target that links it (PUBLIC)
option(EDPLUGIN_COMMON_UBSAN_TRAP "UBSan in trap mode for the C++ of the library and its linking targets (a test build)"
       OFF)
if(EDPLUGIN_COMMON_UBSAN_TRAP)
  message(WARNING "edplugin_common: EDPLUGIN_COMMON_UBSAN_TRAP is ON - a test build, not one for players")
  # C++ only: a plugin's MASM sources (edloader's exports) go to llvm-ml64, which takes no such option
  target_compile_options(edplugin_common
                         PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:-fsanitize=undefined;-fsanitize-trap=undefined>")
  # for a plugin's log: the build says it is a test build
  target_compile_definitions(edplugin_common PUBLIC EDPLUGIN_COMMON_UBSAN_TRAP=1)
endif()

# a target of the project built with LTO is said at the end of the top directory: guard.cc stays out of it, but a
# plugin's own code with __try is not checked
function(edplugin_common_lto_check)
  get_property(targets DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(ipo ${target} INTERPROCEDURAL_OPTIMIZATION)
    if(ipo)
      message(WARNING "edplugin_common: ${target} is built with INTERPROCEDURAL_OPTIMIZATION; the library's guard.cc is "
                      "kept out of LTO, a __try of the target's own is not (include/edplugin_common/guard/guard.h)")
    endif()
  endforeach()
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL edplugin_common_lto_check)

if(PROJECT_IS_TOP_LEVEL)
  option(EDPLUGIN_COMMON_TESTS "build the test programs (tools/test.sh runs them under wine)" ON)
  if(EDPLUGIN_COMMON_TESTS)
    # the library's own checks: a plain program, without d3d11 or dxgi
    # each header alone in a translation unit of its own: it includes what it needs
    add_executable(
      edplugin_common_test
      test/library_test.cc
      test/log_test.cc
      test/modules_test.cc
      test/settings_doc_test.cc
      test/ini_test.cc
      test/pe_test.cc
      test/guard_test.cc
      test/context_test.cc
      test/alone_lock.cc
      test/alone_once_flag.cc
      test/alone_format.cc
      test/alone_log.cc
      test/alone_state_log.cc
      test/alone_paths.cc
      test/alone_file.cc
      test/alone_memory.cc
      test/alone_pass_modules.cc
      test/alone_shader.cc
      test/alone_names_of.cc
      test/alone_dxgi.cc
      test/alone_device_removed.cc
      test/alone_settings_doc.cc
      test/alone_ini.cc
      test/alone_ini_text.cc
      test/alone_ini_table.cc
      test/alone_ini_live.cc
      test/alone_ini_view.cc
      test/alone_guard.cc
      test/alone_pe.cc
      test/alone_pe_range.cc
      test/alone_context_rule.cc
      test/alone_contract.cc
      test/alone_own_table_mark.cc
      test/alone_own_table.cc
      test/alone_slot_plan.cc
      test/alone_slot.cc
      test/alone_watch_class.cc
      test/alone_watch.cc
      test/alone_multithread.cc
      test/alone_system_d3d11.cc
      test/alone_context.cc)
    target_link_libraries(edplugin_common_test PRIVATE edplugin_common::edplugin_common)
    edplugin_common_warnings(edplugin_common_test)
    # a dll with one export by a known prefix, beside the test (first_export_with_prefix)
    add_library(test_exports SHARED test/test_exports.cc)
    edplugin_common_warnings(test_exports)
    add_dependencies(edplugin_common_test test_exports)
    # the version resource as a plugin's dll has it, read by test/version_test.sh
    edplugin_common_version(EDPLUGIN_COMMON_TEST_VERSION test_exports_version.h test_exports)
  endif()
  option(EDPLUGIN_COMMON_CONTEXT_PROBE "build tools/context_probe (a diagnosis run by hand on Windows) and its test" ON)
  if(EDPLUGIN_COMMON_CONTEXT_PROBE)
    add_subdirectory(tools/context_probe)
  endif()
  option(EDPLUGIN_COMMON_EVENT_REPORT
         "build tools/event_report (the game's events from Windows' logs, run by hand on Windows) and its test" ON)
  if(EDPLUGIN_COMMON_EVENT_REPORT)
    add_subdirectory(tools/event_report)
  endif()
endif()
