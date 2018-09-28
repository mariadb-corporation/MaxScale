# Find ThreadSanitizer libraries
#
# The following variables are set:
# TSAN_FOUND - If ThreadSanitizer was found
# TSAN_LIBRARIES - Path to the libasan library

find_library(TSAN_LIBRARIES NAMES libtsan.so.0 libtsan.so.1 libtsan.so.2 libtsan.so.3 libtsan.so.4)

if (TSAN_LIBRARIES)
  message(STATUS "Found ThreadSanitizer libraries: ${TSAN_LIBRARIES}")
  set(TSAN_FOUND TRUE CACHE INTERNAL "")
else()
  message(STATUS "Could not find ThreadSanitizer")
endif()
