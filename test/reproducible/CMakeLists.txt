# A dll built as a plugin's (debug information in the objects, /DEBUG:FULL, a version resource), for
# test/reproducible_test.sh: built twice in one build directory, the second time from clean, it has to come out the
# same. Configured with a build's toolchain, not a part of the library's build.
cmake_minimum_required(VERSION 3.29)
project(reproducible LANGUAGES CXX)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/edplugin_common.cmake)

add_library(reproducible SHARED reproducible.cc)
target_compile_features(reproducible PRIVATE cxx_std_23)
target_link_options(reproducible PRIVATE LINKER:/DEBUG:FULL $<$<NOT:$<CONFIG:Debug>>:LINKER:/OPT:REF,/OPT:ICF>)
edplugin_common_version(REPRODUCIBLE_VERSION reproducible_version.h reproducible)
