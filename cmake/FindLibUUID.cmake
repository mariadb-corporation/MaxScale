# This CMake file tries to find the libuuid and libuuid-develop
# The following variables are set:
# LIBUUID_LIBRARIES - System has libuuid
# LIBUUID_HEADERS - The libuuid headers

find_library(LIBUUID_LIBRARIES NAMES uuid)
if (NOT LIBUUID_LIBRARIES)
  message(STATUS "Could not find libuuid library")
else()
  message(STATUS "Found libuuid ${LIBUUID_LIBRARIES}")
endif()
find_path(LIBUUID_HEADERS uuid.h PATH_SUFFIXES uuid/)
if (NOT LIBUUID_HEADERS)
  message(STATUS "Could not find libuuid headers")
else()
  message(STATUS "Found libuuid headers ${LIBUUID_HEADERS}")
endif()
