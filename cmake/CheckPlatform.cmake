#Checks for all the C system headers found in all the files

include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckIncludeFiles)
include(CheckCXXSourceCompiles)

# Check for libraries MaxScale depends on
find_library(HAVE_LIBSSL NAMES ssl)
if(NOT HAVE_LIBSSL)
  message(FATAL_ERROR "Could not find libssl")
endif()

find_library(HAVE_LIBCRYPT NAMES crypt)
if(NOT HAVE_LIBCRYPT)
  message(FATAL_ERROR "Could not find libcrypt")
endif()

find_library(HAVE_LIBCRYPTO NAMES crypto)
if(NOT HAVE_LIBCRYPTO)
  message(FATAL_ERROR "Could not find libcrypto")
endif()

find_library(HAVE_LIBZ NAMES z)
if(NOT HAVE_LIBZ)
  message(FATAL_ERROR "Could not find libz")
endif()

find_library(HAVE_LIBM NAMES m)
if(NOT HAVE_LIBM)
  message(FATAL_ERROR "Could not find libm")
endif()

find_library(HAVE_LIBRT NAMES rt)
if(NOT HAVE_LIBRT)
  message(FATAL_ERROR "Could not find librt")
endif()

find_library(HAVE_LIBPTHREAD NAMES pthread)
if(NOT HAVE_LIBPTHREAD)
  message(FATAL_ERROR "Could not find libpthread")
endif()

# The XSI version of strerror_r return an int and the GNU version a char*
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
