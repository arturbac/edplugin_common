# The build steps every plugin shares, as functions (CMake functions are global: a plugin calls them after
# add_subdirectory(edplugin_common)). Their generated headers go to <the plugin's build dir>/gen; the library's own
# files are found from this file's directory, so they work in the plugin's scope.

# edplugin_common_version(<macro> <header file name> <target>...): the header <build>/gen/<header> with <macro> defined
# to the plugin's version (version.cmake, at every build), forced into the C++ sources of each target (not into
# MASM sources); the custom target is named after the header without its extension
function(edplugin_common_version macro header)
  set(gen "${CMAKE_BINARY_DIR}/gen")
  file(MAKE_DIRECTORY "${gen}")
  cmake_path(GET header STEM name)
  add_custom_target(${name}
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${gen}/${header} -DNAME=${macro}
            -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/version.cmake
    BYPRODUCTS "${gen}/${header}"
    COMMENT "${name}"
    VERBATIM)
  foreach(target IN LISTS ARGN)
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/FI${gen}/${header}>")
    add_dependencies(${target} ${name})
  endforeach()
endfunction()

# edplugin_common_pin(<target>...): the check of the clone against the plugin's edplugin_common.commit (pin.cmake, at
# every build: a warning, an error in a release build) and the header <build>/gen/edplugin_common_commit.h with
# EDPLUGIN_COMMON_COMMIT, reachable from each target's sources
function(edplugin_common_pin)
  set(gen "${CMAKE_BINARY_DIR}/gen")
  file(MAKE_DIRECTORY "${gen}")
  if(NOT TARGET edplugin_common_pin)
    cmake_path(GET CMAKE_CURRENT_FUNCTION_LIST_DIR PARENT_PATH clone)
    add_custom_target(edplugin_common_pin
      COMMAND ${CMAKE_COMMAND} -DCLONE=${clone} -DPIN=${CMAKE_SOURCE_DIR}/edplugin_common.commit -DSRC=${CMAKE_SOURCE_DIR}
              -DOUT=${gen}/edplugin_common_commit.h -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/pin.cmake
      BYPRODUCTS "${gen}/edplugin_common_commit.h"
      COMMENT "edplugin_common pin"
      VERBATIM)
  endif()
  foreach(target IN LISTS ARGN)
    target_include_directories(${target} PRIVATE "${gen}")
    add_dependencies(${target} edplugin_common_pin)
  endforeach()
endfunction()

# edplugin_common_fxc(<out> <profile> <entry> <variable> <source> [fxc options...]): <out>, a header with the shader
# compiled by the Windows SDK's fxc.exe under wine (the newest one in MSVC_ROOT, or EDPLUGIN_COMMON_FXC; wine by
# EDPLUGIN_COMMON_WINE), looked for at the first call. fxc is a Windows program: options start with '/', so the paths
# it gets are relative to the build directory (outputs) or Windows paths through wine's Z: drive (inputs)
function(edplugin_common_fxc out profile entry var source)
  find_program(EDPLUGIN_COMMON_WINE wine REQUIRED)
  if(NOT EDPLUGIN_COMMON_FXC)
    file(GLOB fxc_found "${MSVC_ROOT}/Windows Kits/10/bin/*/x64/fxc.exe")
    list(SORT fxc_found COMPARE NATURAL)
    list(POP_BACK fxc_found fxc_newest)
    # FORCE: an empty entry left by an earlier configure that found none is replaced
    set(EDPLUGIN_COMMON_FXC "${fxc_newest}" CACHE FILEPATH "fxc.exe of a Windows SDK, run under wine" FORCE)
  endif()
  if(NOT EXISTS "${EDPLUGIN_COMMON_FXC}")
    message(FATAL_ERROR "EDPLUGIN_COMMON_FXC='${EDPLUGIN_COMMON_FXC}': no fxc.exe (looked in ${MSVC_ROOT}/Windows Kits/10/bin/*/x64)")
  endif()
  cmake_path(RELATIVE_PATH out BASE_DIRECTORY "${CMAKE_BINARY_DIR}" OUTPUT_VARIABLE out_rel)
  add_custom_command(
    OUTPUT "${out}"
    COMMAND ${CMAKE_COMMAND} -E env WINEDEBUG=-all
            "${EDPLUGIN_COMMON_WINE}" "${EDPLUGIN_COMMON_FXC}" /nologo ${ARGN} /T ${profile} /E ${entry} /Vn ${var}
            /Fh "${out_rel}" "Z:${source}"
    DEPENDS "${source}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "fxc ${profile} ${var}"
    VERBATIM)
endfunction()
