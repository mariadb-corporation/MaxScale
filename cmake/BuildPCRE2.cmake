# Build the PCRE2 library from source
#
# This will add a 'pcre2' target to CMake which will generate the libpcre2-8.so
# dynamic library and the pcre2.h header. If your target requires PCRE2 you
# need to add a dependeny on the 'pcre2' target by adding add_dependencies(<target> pcre2)
# to the CMakeLists.txt

include(ExternalProject)

set(PCRE2_ROOT_DIR ${CMAKE_SOURCE_DIR}/pcre2/)
set(PCRE2_BUILD_DIR ${CMAKE_BINARY_DIR}/pcre2/)
set(PCRE2_LIBRARIES ${CMAKE_BINARY_DIR}/pcre2/libpcre2-8.so
  ${CMAKE_BINARY_DIR}/pcre2/libpcre2-8.so.1.0.0
  CACHE STRING "PCRE2 dynamic libraries" FORCE)

ExternalProject_Add(pcre2 SOURCE_DIR ${PCRE2_ROOT_DIR}
  CMAKE_ARGS -DBUILD_SHARED_LIBS=Y -DPCRE2_BUILD_PCRE2GREP=N -DPCRE2_BUILD_TESTS=N
  BINARY_DIR ${PCRE2_BUILD_DIR}
  BUILD_COMMAND make
  INSTALL_COMMAND "")

include_directories(${CMAKE_BINARY_DIR}/pcre2/)
install(PROGRAMS ${PCRE2_LIBRARIES} DESTINATION ${MAXSCALE_LIBDIR})
