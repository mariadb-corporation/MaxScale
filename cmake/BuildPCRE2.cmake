# Build the PCRE2 library from source
#
# This will add a 'pcre2' target to CMake which will generate the libpcre2-8.a
# static library and the pcre2.h header. If your target requires PCRE2 you
# need to add a dependeny on the 'pcre2' target by adding add_dependencies(<target> pcre2)
# to the CMakeLists.txt. You don't need to link against the pcre2 library
# because the static symbols will be in MaxScale.

set(PCRE2_REPO "https://github.com/PCRE2Project/pcre2.git" CACHE STRING "PCRE2 Git repository")

set(PCRE2_TAG "pcre2-10.36" CACHE STRING "PCRE2 Git tag")

message(STATUS "Using pcre2 version ${PCRE2_TAG}")

set(PCRE2_BASE "${CMAKE_BINARY_DIR}/pcre2")

set(PCRE2_SOURCE "${PCRE2_BASE}/src")
set(PCRE2_BINARY "${PCRE2_BASE}/build")

ExternalProject_Add(pcre2
  GIT_REPOSITORY ${PCRE2_REPO}
  GIT_TAG ${PCRE2_TAG}
  GIT_SHALLOW TRUE
  SOURCE_DIR ${PCRE2_SOURCE}
  CMAKE_ARGS -DCMAKE_C_FLAGS=-fPIC -DBUILD_SHARED_LIBS=N -DPCRE2_BUILD_PCRE2GREP=N  -DPCRE2_BUILD_TESTS=N -DPCRE2_SUPPORT_JIT=Y
  BINARY_DIR ${PCRE2_BINARY}
  BUILD_COMMAND make
  INSTALL_COMMAND ""
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

set(PCRE2_INCLUDE_DIRS ${PCRE2_BINARY} CACHE PATH "PCRE2 headers" FORCE)
set(PCRE2_LIBRARIES ${PCRE2_BINARY}/libpcre2-8.a CACHE PATH "PCRE2 libraries" FORCE)
set(PCRE2_FOUND TRUE CACHE BOOL "Found PCRE2 libraries" FORCE)
