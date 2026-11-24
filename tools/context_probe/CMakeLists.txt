# context_probe: a program for another Windows, run by hand (no game), that tells where the D3D11 runtime keeps the
# method tables of a device and of its context, how it rewrites them, and what else in the process changed them
# (README.txt beside it). context_probe_test checks its following of entries without d3d11 (tools/test.sh, wine).
add_executable(context_probe context_probe.cc follow.cc context_probe.rc context_probe.manifest)
target_link_libraries(context_probe PRIVATE edplugin_common::edplugin_common)
edplugin_common_warnings(context_probe)

add_executable(context_probe_test follow_test.cc follow.cc)
target_link_libraries(context_probe_test PRIVATE edplugin_common::edplugin_common)
edplugin_common_warnings(context_probe_test)
# beside the library's test programs: tools/test.sh takes them from the build directory
set_target_properties(context_probe context_probe_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
