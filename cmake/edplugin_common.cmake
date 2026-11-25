# The build steps every plugin shares, as functions (CMake functions are global: a plugin calls them after
# add_subdirectory(edplugin_common)). The files they make go to <the plugin's build dir>/gen; the library's own
# files are found from this file's directory, so they work in the plugin's scope. Only function definitions stand
# here, so a script run by cmake -P can include this file (test/proxy_exports_test.sh does).

# edplugin_common_version(<macro> <header file name> <target>...): the header <build>/gen/<header> with <macro> defined
# to the plugin's version, <macro>_COMMIT to its full commit and <macro>_BUILD_TIME to its time (version.cmake, at
# every build), forced into the C++ sources of each target (not into MASM sources); with the resource compiler (a
# Windows build) each exe or dll target gets the version resource <build>/gen/<target>_version.rc as a source (a
# static library would lose it at its link); the custom target
# is named after the header without its extension
function(edplugin_common_version macro header)
  set(gen "${CMAKE_BINARY_DIR}/gen")
  file(MAKE_DIRECTORY "${gen}")
  cmake_path(GET header STEM name)
  set(rc_args "")
  set(rc_files "")
  set(targets "")
  get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
  if(RC IN_LIST languages AND ARGN)
    set(files "")
    set(types "")
    foreach(target IN LISTS ARGN)
      get_target_property(type ${target} TYPE)
      if(type STREQUAL "EXECUTABLE")
        list(APPEND types 1)
      elseif(type STREQUAL "SHARED_LIBRARY" OR type STREQUAL "MODULE_LIBRARY")
        list(APPEND types 2)
      else()
        continue()
      endif()
      list(APPEND targets ${target})
      list(APPEND rc_files "${gen}/${target}_version.rc")
      list(APPEND files "$<TARGET_FILE_NAME:${target}>")
    endforeach()
    list(JOIN targets "," rc_targets)
    list(JOIN files "," files)
    list(JOIN types "," types)
    if(targets)
      set(rc_args -DRC_DIR=${gen} -DRC_TARGETS=${rc_targets} -DRC_FILES=${files} -DRC_TYPES=${types}
                  -DPRODUCT=${PROJECT_NAME})
    endif()
  endif()
  add_custom_target(${name}
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${gen}/${header} -DNAME=${macro} ${rc_args}
            -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/version.cmake
    BYPRODUCTS "${gen}/${header}" ${rc_files}
    COMMENT "${name}"
    VERBATIM)
  foreach(target rc IN ZIP_LISTS targets rc_files)
    target_sources(${target} PRIVATE "${rc}")
  endforeach()
  foreach(target IN LISTS ARGN)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/FI${gen}/${header}>")
    else()
      target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-include${gen}/${header}>")
    endif()
    add_dependencies(${target} ${name})
  endforeach()
endfunction()

# edplugin_common_warnings(<target>...): the warning level the plugins build with, on the C++ sources of each target:
# /W4 with clang-cl, the same -Wall -Wextra with clang's GNU-style driver (cmake/clang-msvc-sdk.cmake)
function(edplugin_common_warnings)
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(level /W4)
  else()
    set(level -Wall -Wextra)
  endif()
  foreach(target IN LISTS ARGN)
    foreach(option IN LISTS level)
      target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${option}>")
    endforeach()
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

# the line (from 1) of the character at <pos> in <text>
function(_edplugin_common_line_of text pos out)
  string(SUBSTRING "${text}" 0 ${pos} before)
  string(REGEX MATCHALL "\n" newlines "${before}")
  list(LENGTH newlines count)
  math(EXPR line "${count} + 1")
  set(${out} ${line} PARENT_SCOPE)
endfunction()

# edplugin_common_proxy_exports(<tag> <list> [WRAP <name>...]): the exports of a proxy dll that stands in for a system
# dll, from <list> (a text file, relative to the calling directory: one export name a line, '#' starts a comment
# anywhere in a line, spaces and empty lines ignored). Made at configure time (script commands only, so it also runs in
# cmake -P), in <build>/gen:
#   <tag>_proxy.def        - EXPORTS: each name not in WRAP as <tag>_proxy_thunk_<i>, then each WRAP name as
#                            <tag>_proxy_impl_<name>; no ordinals
#   <tag>_proxy_thunks.asm - MASM: the slot table <tag>_proxy_real_procs (one pointer per name not in WRAP),
#                            <tag>_proxy_unresolved (returns 0) and the thunks: <tag>_proxy_thunk_<i> jumps through
#                            slot i, where i counts the names not in WRAP in the list's order
#   <tag>_proxy.h          - <tag>_proxy_names (the names not in WRAP, in slot order), <tag>_proxy_count, and the
#                            C declarations of the slot table and of <tag>_proxy_unresolved
# The plugin fills every slot before the first call (GetProcAddress on the next module, <tag>_proxy_unresolved where
# it has none) and defines <tag>_proxy_impl_<name> for each WRAP name. A file is rewritten only when its text changes,
# so an unchanged list rebuilds nothing; a change of the list configures again. Sets <tag>_PROXY_DEF, <tag>_PROXY_ASM
# and <tag>_PROXY_HEADER in the caller's scope. Errors (nothing written): a tag or name that is not a C identifier, a
# name twice, a WRAP name twice or not in the list, no name left outside WRAP, ';', '[', ']' or a byte outside ASCII
# in the list outside comments.
function(edplugin_common_proxy_exports tag list)
  cmake_parse_arguments(PARSE_ARGV 2 arg "" "" WRAP)
  set(what "edplugin_common_proxy_exports")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${what}: unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  set(identifier "^[A-Za-z_][A-Za-z0-9_]*$")
  if(NOT tag MATCHES "${identifier}")
    message(FATAL_ERROR "${what}: the tag '${tag}' is not a C identifier")
  endif()
  cmake_path(ABSOLUTE_PATH list BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE source)
  if(NOT EXISTS "${source}")
    message(FATAL_ERROR "${what}: no list ${source}")
  endif()
  file(READ "${source}" text)
  # the comments out first, their line ends kept (the lines keep their numbers): what a comment says is not checked
  string(REGEX REPLACE "#[^\n]*" "" text "${text}")

  # a CMake list is split at ';' and bracketed by '[' ']', and string commands work on bytes: such a list is refused
  # whole, before it is split into lines
  foreach(bad ";" "[" "]")
    string(FIND "${text}" "${bad}" pos)
    if(pos GREATER_EQUAL 0)
      _edplugin_common_line_of("${text}" ${pos} line)
      message(FATAL_ERROR "${what}: ${source}:${line}: '${bad}' in the list")
    endif()
  endforeach()
  string(REGEX MATCH "[^\t\r\n -~]" byte "${text}")
  if(NOT byte STREQUAL "")
    string(FIND "${text}" "${byte}" pos)
    _edplugin_common_line_of("${text}" ${pos} line)
    message(FATAL_ERROR "${what}: ${source}:${line}: a character outside ASCII in the list")
  endif()

  string(REPLACE "\n" ";" lines "${text}")
  set(names)
  set(line 0)
  foreach(entry IN LISTS lines)
    math(EXPR line "${line} + 1")
    string(FIND "${entry}" "#" hash)
    if(hash GREATER_EQUAL 0)
      string(SUBSTRING "${entry}" 0 ${hash} entry)
    endif()
    string(STRIP "${entry}" name)
    if(name STREQUAL "")
      continue()
    endif()
    if(NOT name MATCHES "${identifier}")
      message(FATAL_ERROR "${what}: ${source}:${line}: '${name}' is not a C identifier")
    endif()
    if(name IN_LIST names)
      message(FATAL_ERROR "${what}: ${source}:${line}: '${name}' listed twice")
    endif()
    list(APPEND names "${name}")
  endforeach()
  # lengths, not if(<list>): a single name such as Off or NO is a false constant to if()
  list(LENGTH names named)
  if(named EQUAL 0)
    message(FATAL_ERROR "${what}: ${source}: no name in the list")
  endif()

  set(wrapped)
  foreach(name IN LISTS arg_WRAP)
    if(NOT name IN_LIST names)
      message(FATAL_ERROR "${what}: WRAP ${name}: not in ${source}")
    endif()
    if(name IN_LIST wrapped)
      message(FATAL_ERROR "${what}: WRAP ${name} given twice")
    endif()
    list(APPEND wrapped "${name}")
  endforeach()
  # both in the list's order
  set(thunked)
  set(wrapped_in_order)
  foreach(name IN LISTS names)
    if(name IN_LIST wrapped)
      list(APPEND wrapped_in_order "${name}")
    else()
      list(APPEND thunked "${name}")
    endif()
  endforeach()
  set(wrapped ${wrapped_in_order})
  list(LENGTH thunked count)
  if(count EQUAL 0)
    message(FATAL_ERROR "${what}: every name in ${source} is in WRAP: no thunk left")
  endif()

  cmake_path(GET source FILENAME source_name)
  set(made "${tag} proxy exports, made by edplugin_common_proxy_exports from ${source_name}")
  set(def "; ${made}\nEXPORTS\n")
  string(CONCAT asm "; ${made}\n; Each thunk is one indirect jmp through its slot: no prologue, the caller's frame and arguments\n"
          "; reach the target as they are.\n\n.DATA\n\nPUBLIC ${tag}_proxy_real_procs\n"
          "${tag}_proxy_real_procs QWORD ${count} DUP(0)\n\n.CODE\n\n"
          "PUBLIC ${tag}_proxy_unresolved\n${tag}_proxy_unresolved PROC\n    xor eax, eax\n    ret\n"
          "${tag}_proxy_unresolved ENDP\n")
  set(header_names)
  set(i 0)
  foreach(name IN LISTS thunked)
    math(EXPR offset "${i} * 8")
    string(APPEND def "    ${name} = ${tag}_proxy_thunk_${i}\n")
    string(APPEND asm "\nPUBLIC ${tag}_proxy_thunk_${i}\n${tag}_proxy_thunk_${i} PROC\n"
                      "    jmp QWORD PTR [${tag}_proxy_real_procs + ${offset}]\n${tag}_proxy_thunk_${i} ENDP\n")
    string(APPEND header_names "  \"${name}\",\n")
    math(EXPR i "${i} + 1")
  endforeach()
  foreach(name IN LISTS wrapped)
    string(APPEND def "    ${name} = ${tag}_proxy_impl_${name}\n")
  endforeach()
  string(APPEND asm "\nEND\n")
  string(CONCAT header
    "// ${made}\n#pragma once\n\n#include <cstddef>\n\n"
    "inline constexpr std::size_t ${tag}_proxy_count{${count}};\n"
    "// the names of ${tag}_proxy_real_procs's slots, in its order\n"
    "inline constexpr char const * ${tag}_proxy_names[]{\n${header_names}};\n"
    "static_assert(sizeof(${tag}_proxy_names) / sizeof(${tag}_proxy_names[0]) == ${tag}_proxy_count);\n\n"
    "// ${tag}_proxy_impl_<name> of each wrapped name is declared and defined by the plugin, with that function's "
    "signature\nextern \"C\"\n  {\n"
    "  // ${tag}_proxy_thunk_<i> jumps through slot i; every slot is filled before the first call\n"
    "  extern void * ${tag}_proxy_real_procs[${tag}_proxy_count];\n"
    "  // returns 0: the slot of a function the next module does not have\n"
    "  void ${tag}_proxy_unresolved();\n  }\n")

  set(gen "${CMAKE_BINARY_DIR}/gen")
  file(MAKE_DIRECTORY "${gen}")
  set(outputs)
  foreach(kind def asm header)
    if(kind STREQUAL "def")
      set(out "${gen}/${tag}_proxy.def")
    elseif(kind STREQUAL "asm")
      set(out "${gen}/${tag}_proxy_thunks.asm")
    else()
      set(out "${gen}/${tag}_proxy.h")
    endif()
    file(WRITE "${out}.tmp" "${${kind}}")
    file(COPY_FILE "${out}.tmp" "${out}" ONLY_IF_DIFFERENT)
    file(REMOVE "${out}.tmp")
    list(APPEND outputs "${out}")
  endforeach()
  if(NOT CMAKE_SCRIPT_MODE_FILE)
    # a changed list, or a deleted output, configures again
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}" ${outputs})
  endif()
  set(${tag}_PROXY_DEF "${gen}/${tag}_proxy.def" PARENT_SCOPE)
  set(${tag}_PROXY_ASM "${gen}/${tag}_proxy_thunks.asm" PARENT_SCOPE)
  set(${tag}_PROXY_HEADER "${gen}/${tag}_proxy.h" PARENT_SCOPE)
endfunction()
