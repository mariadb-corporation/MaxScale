# Build the PCRE2 library from source
#
# This will add a 'pcre2' target to CMake which will generate the libpcre2-8.a
# static library and the pcre2.h header. If your target requires PCRE2 you
# need to add a dependeny on the 'pcre2' target by adding add_dependencies(<target> pcre2)
# to the CMakeLists.txt. You don't need to link against the pcre2 library
# because the static symbols will be in MaxScale.

include(ExternalProject)

ExternalProject_Add(pcre2 SOURCE_DIR ${CMAKE_SOURCE_DIR}/pcre2/
  CMAKE_ARGS -DBUILD_SHARED_LIBS=N -DPCRE2_BUILD_PCRE2GREP=N  -DPCRE2_BUILD_TESTS=N
  BINARY_DIR ${CMAKE_BINARY_DIR}/pcre2/
  BUILD_COMMAND make
  INSTALL_COMMAND "")

include_directories(${CMAKE_BINARY_DIR}/pcre2/)
set(PCRE2_LIBRARIES ${CMAKE_BINARY_DIR}/pcre2/libpcre2-8.a CACHE INTERNAL "")
