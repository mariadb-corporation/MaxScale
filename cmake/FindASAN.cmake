# Find AddressSanitizer libraries
#
# The following variables are set:
# ASAN_FOUND - If AddressSanitizer was found
# ASAN_LIBRARIES - Path to the libasan library

find_library(ASAN_LIBRARIES NAMES libasan.so.0 libasan.so.3 libasan.so.4)

if (ASAN_LIBRARIES)
  message(STATUS "Found AdressSanitizer libraries: ${ASAN_LIBRARIES}")
  set(ASAN_FOUND TRUE CACHE INTERNAL "")
else()
  message(STATUS "Could not find AdressSanitizer")
endif()
