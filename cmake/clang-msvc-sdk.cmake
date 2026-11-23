# The same cross build as clang-cl-msvc-sdk.cmake (64-bit Windows on Linux, MSVC's and the Windows SDK's headers and
# libraries from MSVC_ROOT), with clang's GNU-style driver instead of clang-cl: for an IDE whose code model is libclang
# reading compile_commands.json (KDevelop), which understands GNU-style options only. clang targets
# x86_64-pc-windows-msvc, so the ABI, the runtime and the code are MSVC's as with clang-cl; the plugins' releases are
# built with clang-cl-msvc-sdk.cmake. MSVC_ROOT: as there; -Xmicrosoft-windows-sys-root is the driver's /winsysroot
# (the same headers in the same order after clang's own, the same libraries for lld-link).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT MSVC_ROOT AND DEFINED ENV{MSVC_ROOT})
  set(MSVC_ROOT "$ENV{MSVC_ROOT}")
endif()
if(NOT MSVC_ROOT OR NOT IS_DIRECTORY "${MSVC_ROOT}/VC/Tools/MSVC" OR NOT IS_DIRECTORY "${MSVC_ROOT}/Windows Kits/10")
  message(FATAL_ERROR "MSVC_ROOT='${MSVC_ROOT}': set it (cache or environment) to a directory with VC/Tools/MSVC and Windows Kits/10")
endif()
set(MSVC_ROOT "${MSVC_ROOT}" CACHE PATH "MSVC and the Windows SDK in clang's /winsysroot layout")
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MSVC_ROOT)

find_program(CMAKE_C_COMPILER clang REQUIRED)
find_program(CMAKE_CXX_COMPILER clang++ REQUIRED)
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
# GNU-style archives (CMake drives ar for this driver); lld-link reads them as it reads lib.exe's
find_program(CMAKE_AR llvm-ar REQUIRED)
find_program(CMAKE_RANLIB llvm-ranlib REQUIRED)
find_program(CMAKE_RC_COMPILER llvm-rc REQUIRED)
find_program(CMAKE_MT llvm-mt REQUIRED)
find_program(CMAKE_ASM_MASM_COMPILER llvm-ml64 REQUIRED)

set(CMAKE_C_FLAGS_INIT "-Xmicrosoft-windows-sys-root ${MSVC_ROOT}")
set(CMAKE_CXX_FLAGS_INIT "-Xmicrosoft-windows-sys-root ${MSVC_ROOT}")
foreach(kind EXE SHARED MODULE)
  set(CMAKE_${kind}_LINKER_FLAGS_INIT "-Xmicrosoft-windows-sys-root ${MSVC_ROOT}")
endforeach()
# as clang-cl compiles: -std=c++23, not gnu++23, and no module scan (its @<file>.modmap in the compile commands is for
# the build, not for a code model)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_FIND_ROOT_PATH "${MSVC_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
