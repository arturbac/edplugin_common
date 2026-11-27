# context_probe: a program for another Windows, run by hand (no game), that tells where the D3D11 runtime keeps the
# method tables of a device and of its context, how it rewrites them, and what else in the process changed them, how
# the library's context hooks (hooks.cc) keep through calls, the multithread protection and ClearState, also after a
# swap chain on a window never shown, a few Present and a ResizeBuffers (README.txt beside it). context_probe_test (with
# EDPLUGIN_COMMON_TESTS) checks its following of entries and its hidden window without d3d11 (tools/test.sh, wine).
add_executable(context_probe context_probe.cc follow.cc hooks.cc window.cc context_probe.rc context_probe.manifest)
target_link_libraries(context_probe PRIVATE edplugin_common::edplugin_common)
edplugin_common_warnings(context_probe)

# beside the library's test programs: tools/test.sh takes them from the build directory
set_target_properties(context_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

if(EDPLUGIN_COMMON_TESTS)
  add_executable(context_probe_test follow_test.cc follow.cc window.cc)
  target_link_libraries(context_probe_test PRIVATE edplugin_common::edplugin_common)
  edplugin_common_warnings(context_probe_test)
  set_target_properties(context_probe_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()
