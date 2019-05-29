# Find UBSan libraries
#
# The following variables are set:
# UBSAN_FOUND - If UBSan was found
# UBSAN_LIBRARIES - Path to the libubsan library

find_library(UBSAN_LIBRARIES NAMES libubsan.so.0 libubsan.so.1 libubsan.so.2 libubsan.so.3 libubsan.so.4)

if (UBSAN_LIBRARIES)
  message(STATUS "Found UBSan libraries: ${UBSAN_LIBRARIES}")
  set(UBSAN_FOUND TRUE CACHE INTERNAL "")
else()
  message(STATUS "Could not find UBSan")
endif()
