# Run at build time (cmake -DSRC=<checkout> -DOUT=<header> -DNAME=<macro> [-DRC_DIR=<dir> -DRC_TARGETS=<a,b>
# -DRC_FILES=<a.dll,b.dll> -DRC_TYPES=<2,2> -DPRODUCT=<name>] -P version.cmake): writes the header with NAME defined to
# the build's version, NAME_COMMIT to its full commit and NAME_BUILD_TIME to its time, and with RC_DIR a version
# resource <RC_DIR>/<target>_version.rc for each of RC_TARGETS (its file name and FILETYPE, 1 an exe, 2 a dll, at the
# same place in RC_FILES and RC_TYPES); each file rewritten only when it changes.
# The version: a release (HEAD on its tag vX.Y.Z, no uncommitted changes to tracked files) is "vX.Y.Z"; any other build
# is "<the last vX.Y.Z tag>-<HEAD's short hash>", with "-dirty" when tracked files differ from HEAD (or git status
# fails); "v0.0.0-<hash>" before the first tag; "dev" outside git, without a commit, or when git cannot run. A
# pre-release tag (vX.Y.Z-beta.N) counts as a release.
# The commit: HEAD's full hash, "unknown" without one. The time (UTC, 2026-11-24T23:12:05Z): SOURCE_DATE_EPOCH from the
# environment when set, else HEAD's commit time, with "-dirty" as the version has it; "unknown" without either. No
# clock at the build in the header or the resource (the linker's own time stamp in the dll is apart).
# The resource's numbers X,Y,Z,K: the last tag's and K the commits after it (git describe --long; 0 on the tag; all
# commits before the first tag, 0,0,0,K; a last tag not vX.Y.Z..., e.g. v2, 0,0,0,K with a warning); "dev" is 0,0,0,0. Flags: PRERELEASE when the last tag is a pre-release,
# PRIVATEBUILD (with the PrivateBuild text) for any build that is not a release. Texts are printable ASCII.
# A plugin runs it through edplugin_common_version() (cmake/edplugin_common.cmake).
function(git_out var)
  execute_process(COMMAND git ${ARGN}
                  WORKING_DIRECTORY "${SRC}"
                  OUTPUT_VARIABLE out OUTPUT_STRIP_TRAILING_WHITESPACE
                  RESULT_VARIABLE rc ERROR_QUIET)
  if(NOT rc EQUAL 0)
    set(out "")
  endif()
  set(${var} "${out}" PARENT_SCOPE)
endfunction()

# the file written only when its text is not already there
function(write_changed path text)
  set(old "")
  if(EXISTS "${path}")
    file(READ "${path}" old)
  endif()
  if(NOT old STREQUAL text)
    file(WRITE "${path}" "${text}")
  endif()
endfunction()

set(commit unknown)
set(epoch "")
set(numbers 0 0 0 0)
set(prerelease FALSE)
set(dirty FALSE)
git_out(hash rev-parse --short=7 HEAD)
if(hash STREQUAL "")
  set(version dev)
else()
  git_out(commit rev-parse HEAD)
  if(commit STREQUAL "")
    set(commit unknown)
  endif()
  git_out(epoch log -1 --no-show-signature --format=%ct HEAD)
  execute_process(COMMAND git --no-optional-locks status --porcelain --untracked-files=no
                  WORKING_DIRECTORY "${SRC}"
                  OUTPUT_VARIABLE changes OUTPUT_STRIP_TRAILING_WHITESPACE
                  RESULT_VARIABLE rc ERROR_QUIET)
  if(NOT rc EQUAL 0)
    set(changes "?")
  endif()
  git_out(exact describe --tags --exact-match --match "v[0-9]*" HEAD)
  git_out(tag describe --tags --abbrev=0 --match "v[0-9]*" HEAD)
  git_out(described describe --tags --long --match "v[0-9]*" HEAD)
  if(tag STREQUAL "")
    set(tag v0.0.0)
    git_out(count rev-list --count HEAD)
    set(numbers 0 0 0 "${count}")
  elseif(described MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)(.*)-([0-9]+)-g[0-9a-f]+$")
    set(numbers ${CMAKE_MATCH_1} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_5})
    if(CMAKE_MATCH_4 MATCHES "^-")
      set(prerelease TRUE)
    endif()
  elseif(described MATCHES "-([0-9]+)-g[0-9a-f]+$")
    set(numbers 0 0 0 "${CMAKE_MATCH_1}")
    message(WARNING "version.cmake: the tag ${tag} is not vX.Y.Z; the version resource's numbers are 0,0,0,"
                    "${CMAKE_MATCH_1}")
  endif()
  if(NOT exact STREQUAL "" AND changes STREQUAL "")
    set(version "${exact}")
  elseif(changes STREQUAL "")
    set(version "${tag}-${hash}")
  else()
    set(version "${tag}-${hash}-dirty")
    set(dirty TRUE)
  endif()
endif()

if(DEFINED ENV{SOURCE_DATE_EPOCH} AND "$ENV{SOURCE_DATE_EPOCH}" MATCHES "^[0-9]+$")
  set(epoch "$ENV{SOURCE_DATE_EPOCH}")
endif()
if(epoch MATCHES "^[0-9]+$")
  # string(TIMESTAMP) formats SOURCE_DATE_EPOCH instead of the clock
  set(ENV{SOURCE_DATE_EPOCH} "${epoch}")
  string(TIMESTAMP build_time "%Y-%m-%dT%H:%M:%SZ" UTC)
  if(dirty)
    string(APPEND build_time "-dirty")
  endif()
else()
  set(build_time unknown)
endif()

write_changed("${OUT}" "#pragma once\n#define ${NAME} \"${version}\"\n#define ${NAME}_COMMIT \"${commit}\"\n\
#define ${NAME}_BUILD_TIME \"${build_time}\"\n")

if(NOT DEFINED RC_DIR)
  return()
endif()

# a field of FILEVERSION is 16 bits
set(fields "")
foreach(number IN LISTS numbers)
  if(NOT number MATCHES "^[0-9]+$")
    set(number 0)
  elseif(number GREATER 65535)
    set(number 65535)
  endif()
  list(APPEND fields ${number})
endforeach()
list(JOIN fields "," fields)

# VS_FF_PRERELEASE 0x2, VS_FF_PRIVATEBUILD 0x8
set(flags 0)
set(private_build FALSE)
if(prerelease)
  math(EXPR flags "${flags} | 2")
endif()
if(NOT version STREQUAL "${exact}")
  set(private_build TRUE)
  math(EXPR flags "${flags} | 8")
endif()
math(EXPR flags "${flags}" OUTPUT_FORMAT HEXADECIMAL)

# a text of the resource: printable ASCII, a quote doubled
function(rc_text var text)
  string(REGEX REPLACE "[^ -~]" "?" text "${text}")
  string(REPLACE "\"" "\"\"" text "${text}")
  set(${var} "${text}" PARENT_SCOPE)
endfunction()
rc_text(version_text "${version}")
rc_text(product_text "${PRODUCT}")

string(REPLACE "," ";" targets "${RC_TARGETS}")
string(REPLACE "," ";" files "${RC_FILES}")
string(REPLACE "," ";" types "${RC_TYPES}")
foreach(target file type IN ZIP_LISTS targets files types)
  rc_text(target_text "${target}")
  rc_text(file_text "${file}")
  set(private "")
  if(private_build)
    set(private "      VALUE \"PrivateBuild\", \"${version_text}\"\n")
  endif()
  # the numbers instead of winver.h (not on the cross build's resource path): 0x40004 = VOS_NT_WINDOWS32
  write_changed("${RC_DIR}/${target}_version.rc" "\
// made at the build by edplugin_common's cmake/version.cmake
1 VERSIONINFO
FILEVERSION ${fields}
PRODUCTVERSION ${fields}
FILEFLAGSMASK 0x3f
FILEFLAGS ${flags}
FILEOS 0x40004
FILETYPE ${type}
FILESUBTYPE 0
BEGIN
  BLOCK \"StringFileInfo\"
  BEGIN
    BLOCK \"040904b0\"
    BEGIN
      VALUE \"FileDescription\", \"${target_text}\"
      VALUE \"FileVersion\", \"${version_text}\"
      VALUE \"InternalName\", \"${target_text}\"
      VALUE \"OriginalFilename\", \"${file_text}\"
${private}      VALUE \"ProductName\", \"${product_text}\"
      VALUE \"ProductVersion\", \"${version_text}\"
    END
  END
  BLOCK \"VarFileInfo\"
  BEGIN
    VALUE \"Translation\", 0x409, 1200
  END
END
")
endforeach()
