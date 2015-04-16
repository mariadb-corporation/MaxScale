# This CMake file tries to find the Perl regular expression libraries
# The following variables are set:
# PCRE_FOUND - System has the PCRE library
# PCRE_LIBRARIES - The PCRE library file
# PCRE_INCLUDE_DIRS - The folder with the PCRE headers

find_library(PCRE_LIBRARIES NAMES pcre)
find_path(PCRE_INCLUDE_DIRS pcre.h)
if(PCRE_LIBRARIES AND PCRE_INCLUDE_DIRS)
  message(STATUS "PCRE libs: ${PCRE_LIBRARIES}")
  message(STATUS "PCRE include directory: ${PCRE_INCLUDE_DIRS}")
  set(PCRE_FOUND TRUE CACHE INTERNAL "Found PCRE libraries")
else()
  message(STATUS "PCRE not found")
endif()
