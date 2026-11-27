# edplugin_common's headers without a Windows header (memory/pe_range.h, memory/pass_modules.h's reading of a list,
# shader/shader.h, ini/ini_text.h, ini/ini_table.h, ini/live.h, hook/context_rule.h, hook/own_table_mark.h,
# hook/slot_plan.h, hook/watch_class.h, tools/context_probe's jump.h and slots.h) and
# tools/event_report's part without one (report.cc, its checks in report_checks.h), built natively (Linux, clang++) with sanitizers: tools/test-native.sh
# configures it twice, with address,undefined and with thread (then also the control live3_control.cc). A project of
# its own, not a part of the library's build (which targets Windows).
cmake_minimum_required(VERSION 3.29)
project(edplugin_common_native_test LANGUAGES CXX)

set(EDPLUGIN_COMMON_NATIVE_SANITIZE
    "address,undefined"
    CACHE STRING "the -fsanitize= list; thread apart (it does not go with address); empty for none")

add_executable(edplugin_common_native_test native_test.cc ini_native.cc keeper_native.cc ../../tools/event_report/report.cc)
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

# with thread only: the control of live3_settings_t's check, a run ThreadSanitizer has to report (live3_control.cc)
if(EDPLUGIN_COMMON_NATIVE_SANITIZE STREQUAL "thread")
  add_executable(edplugin_common_live3_control live3_control.cc)
  target_include_directories(edplugin_common_live3_control PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
  target_compile_features(edplugin_common_live3_control PRIVATE cxx_std_23)
  target_compile_options(edplugin_common_live3_control PRIVATE -Wall -Wextra ${sanitize})
  target_link_options(edplugin_common_live3_control PRIVATE ${sanitize})
  target_link_libraries(edplugin_common_live3_control PRIVATE Threads::Threads)
endif()
