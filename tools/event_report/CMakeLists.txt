# event_report: a program for a player's Windows, run by hand, that writes Elite Dangerous' events from Windows' logs
# and Windows Error Reporting's saved reports to a text the player reads before sending it back (README.txt beside it).
# event_report_test (with EDPLUGIN_COMMON_TESTS; tools/test.sh runs it under wine) checks its part without Windows under
# Windows' comparison of names, and the result file's names (three writes in one second); test/native checks the part
# without Windows natively.
add_executable(event_report event_report.cc report.cc fold.cc result_file.cc event_report.rc event_report.manifest)
target_link_libraries(event_report PRIVATE edplugin_common::edplugin_common wevtapi delayimp)
# wevtapi.dll is no KnownDLL: delay-loaded, not at the start, when a wevtapi.dll beside the exe would come first; main
# loads it from System32 (after SetDefaultDllDirectories) before its first call, so a failed load is said, not a fault
target_link_options(event_report PRIVATE "LINKER:/DELAYLOAD:wevtapi.dll")
edplugin_common_warnings(event_report)

# beside the library's test programs: tools/test.sh takes them from the build directory
set_target_properties(event_report PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

if(EDPLUGIN_COMMON_TESTS)
  add_executable(event_report_test event_report_test.cc report.cc fold.cc result_file.cc)
  target_link_libraries(event_report_test PRIVATE edplugin_common::edplugin_common)
  edplugin_common_warnings(event_report_test)
  set_target_properties(event_report_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()
