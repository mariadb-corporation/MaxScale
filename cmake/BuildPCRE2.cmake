# Build the PCRE2 library from source
#
# This will add a 'pcre2' target to CMake which will generate the libpcre2-8.a
# static library and the pcre2.h header. If your target requires PCRE2 you
# need to add a dependeny on the 'pcre2' target by adding add_dependencies(<target> pcre2)
# to the CMakeLists.txt. You don't need to link against the pcre2 library
# because the static symbols will be in MaxScale.
ExternalProject_Add(pcre2 SOURCE_DIR ${CMAKE_SOURCE_DIR}/pcre2/
  CMAKE_ARGS -DCMAKE_C_FLAGS=-fPIC -DBUILD_SHARED_LIBS=N -DPCRE2_BUILD_PCRE2GREP=N  -DPCRE2_BUILD_TESTS=N -DPCRE2_SUPPORT_JIT=Y
  BINARY_DIR ${CMAKE_BINARY_DIR}/pcre2/
  BUILD_COMMAND make
  INSTALL_COMMAND "")

set(PCRE2_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/pcre2/ CACHE PATH "PCRE2 headers" FORCE)
set(PCRE2_LIBRARIES ${CMAKE_BINARY_DIR}/pcre2/libpcre2-8.a CACHE PATH "PCRE2 libraries" FORCE)
set(PCRE2_FOUND TRUE CACHE BOOL "Found PCRE2 libraries" FORCE)
