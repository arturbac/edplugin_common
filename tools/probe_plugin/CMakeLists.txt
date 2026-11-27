# probe_plugin: an edloader plugin for the plugins' tests that only watches (README.txt beside it): a d3d11.dll proxy
# on edworld's skeleton that puts context_probe's three plugins' hooks (../context_probe/hooks.cc) on the first
# device's immediate context and reports every second what they counted, to probe_plugin-<date>-<time>.txt in
# edloader's folder of logs.
enable_language(ASM_MASM)

# exports: the Windows d3d11 names from the list, a thunk each but the two probe_plugin defines (gen/probe_proxy.*)
edplugin_common_proxy_exports(probe d3d11_exports.txt WRAP D3D11CreateDevice D3D11CreateDeviceAndSwapChain)
add_library(probe_plugin_thunks OBJECT "${probe_PROXY_ASM}")
target_compile_options(probe_plugin_thunks PRIVATE /nologo)

add_library(probe_plugin SHARED probe_plugin.cc ../context_probe/hooks.cc ../context_probe/follow.cc
                                "${probe_PROXY_DEF}")
target_include_directories(probe_plugin PRIVATE "${CMAKE_BINARY_DIR}/gen")
target_link_libraries(probe_plugin PRIVATE probe_plugin_thunks edplugin_common::edplugin_common version)
# no tail call to the next d3d11: edloader tells its caller's place in the chain by the return address
target_compile_options(probe_plugin
  PRIVATE $<IF:$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>,/clang:,>-fno-optimize-sibling-calls)
edplugin_common_warnings(probe_plugin)
edplugin_common_version(PROBE_PLUGIN_VERSION probe_plugin_version.h probe_plugin)
set_target_properties(probe_plugin PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
