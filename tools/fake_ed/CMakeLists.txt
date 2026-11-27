# fake_ed: a program in the game's place for the plugins' tests, without the game (README.txt beside it): it imports
# D3D11CreateDevice from d3d11.dll as the game does (edloader beside it is loaded in its place), makes the device as
# the game does, a swap chain on a window shown on the screen, and runs phases of frames as stimuli for a plugin that
# watches the context; each line to fake_ed-<date>-<time>.txt beside it.
add_executable(fake_ed fake_ed.cc fake_ed.rc fake_ed.manifest)
target_link_libraries(fake_ed PRIVATE edplugin_common::edplugin_common d3d11 dxguid)
edplugin_common_warnings(fake_ed)

# beside the library's test programs, as context_probe
set_target_properties(fake_ed PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
