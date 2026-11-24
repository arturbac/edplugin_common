# event_report: a program for a player's Windows, run by hand, that writes Elite Dangerous' events from Windows' logs
# and Windows Error Reporting's saved reports to a text the player reads before sending it back (README.txt beside it).
# event_report_test (with EDPLUGIN_COMMON_TESTS) checks its part without Windows under Windows' comparison of names
# (tools/test.sh, wine); test/native checks the same part natively.
add_executable(event_report event_report.cc report.cc fold.cc event_report.rc event_report.manifest)
target_link_libraries(event_report PRIVATE edplugin_common::edplugin_common wevtapi)
edplugin_common_warnings(event_report)

# beside the library's test programs: tools/test.sh takes them from the build directory
set_target_properties(event_report PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

if(EDPLUGIN_COMMON_TESTS)
  add_executable(event_report_test event_report_test.cc report.cc fold.cc)
  target_link_libraries(event_report_test PRIVATE edplugin_common::edplugin_common)
  edplugin_common_warnings(event_report_test)
  set_target_properties(event_report_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()
