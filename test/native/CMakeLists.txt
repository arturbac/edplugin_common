# edplugin_common's headers without a Windows header (memory/pe_range.h, shader/shader.h, ini/ini_text.h,
# ini/ini_table.h, ini/live.h, tools/context_probe's jump.h and slots.h) and tools/event_report's part without one
# (report.cc, its checks in report_checks.h), built natively (Linux, clang++) with sanitizers: tools/test-native.sh
# configures it twice, with address,undefined and with thread. A project of its own, not a part of the library's build
# (which targets Windows).
cmake_minimum_required(VERSION 3.29)
project(edplugin_common_native_test LANGUAGES CXX)

set(EDPLUGIN_COMMON_NATIVE_SANITIZE
    "address,undefined"
    CACHE STRING "the -fsanitize= list; thread apart (it does not go with address); empty for none")

add_executable(edplugin_common_native_test native_test.cc ini_native.cc ../../tools/event_report/report.cc)
target_include_directories(edplugin_common_native_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include
                                                              ${CMAKE_CURRENT_SOURCE_DIR}/../../tools)
target_compile_features(edplugin_common_native_test PRIVATE cxx_std_23)
target_compile_options(edplugin_common_native_test PRIVATE -Wall -Wextra)
find_package(Threads REQUIRED)
target_link_libraries(edplugin_common_native_test PRIVATE Threads::Threads)
if(EDPLUGIN_COMMON_NATIVE_SANITIZE)
  # the first report ends the program, so a test cannot pass with one
  set(sanitize -fsanitize=${EDPLUGIN_COMMON_NATIVE_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_compile_options(edplugin_common_native_test PRIVATE ${sanitize})
  target_link_options(edplugin_common_native_test PRIVATE ${sanitize})
endif()
