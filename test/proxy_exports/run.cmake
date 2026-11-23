# edplugin_common_proxy_exports in script mode, for test/proxy_exports_test.sh: cmake -DTAG=<tag> -DLIST=<list>
# [-DWRAP=<name;...>] [-DTAG2=<tag> -DLIST2=<list>] -P run.cmake, in the run directory (<build>/gen is ./gen there);
# prints the variables the function sets
cmake_minimum_required(VERSION 3.29)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/edplugin_common.cmake)
edplugin_common_proxy_exports("${TAG}" "${LIST}" WRAP ${WRAP})
message("def ${${TAG}_PROXY_DEF}")
message("asm ${${TAG}_PROXY_ASM}")
message("header ${${TAG}_PROXY_HEADER}")
if(DEFINED TAG2)
  edplugin_common_proxy_exports("${TAG2}" "${LIST2}")
  message("def2 ${${TAG2}_PROXY_DEF}")
endif()
