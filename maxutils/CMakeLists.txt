enable_testing()

project(maxutils)
cmake_minimum_required(VERSION 2.8)
set(CMAKE_CXX_FLAGS "-fPIC -std=c++11 -ggdb -Wall -Werror -Wno-unused-function")

# Check for GLIBC. The XSI version of strerror_r return an int and the GNU version a
# char*. Depending on this check, we make assumptions about the system.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
  #define _GNU_SOURCE 1
  #include <string.h>\n
  int main(){\n
      char errbuf[200];\n
      return strerror_r(13, errbuf, sizeof(errbuf)) == errbuf;\n
  }\n"
  HAVE_GLIBC)

if(HAVE_GLIBC)
  add_definitions(-DHAVE_GLIBC=1)
endif()

add_subdirectory(maxbase)
