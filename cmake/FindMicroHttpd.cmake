# Locates the libmicrohttpd libraries and headers
#
# The following variables are set:
# MICROHTTPD_FOUND - If the libmicrohttpd library was found
# MICROHTTPD_LIBRARIES - Path to the static library
# MICROHTTPD_INCLUDE_DIR - Path to libmicrohttpd headers

find_path(MICROHTTPD_INCLUDE_DIR microhttpd.h)
find_library(MICROHTTPD_LIBRARIES microhttpd)

if (MICROHTTPD_INCLUDE_DIR AND MICROHTTPD_LIBRARIES)
  message(STATUS "Found libmicrohttpd: ${MICROHTTPD_LIBRARIES}")
  set(MICROHTTPD_FOUND TRUE)
elseif(MicroHttpd_FIND_REQUIRED)
  message(FATAL_ERROR "Could not find libmicrohttpd")
else()
  message(STATUS "Could not find libmicrohttpd")
endif()
