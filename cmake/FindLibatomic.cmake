# Find libatomic.so
#
# Some systems (I'm looking at you, CentOS 7) don't have the proper symlink for
# libatomic.so and only have the libatomic.so.1 symlink. This is why we need to
# be a bit smarter than usual when linking the atomic libraries.
#
# The following variables are set:
#
# LIBATOMIC_FOUND - Found libatomic
# LIBATOMIC_LIBRARIES - The libuuid headers

find_library(LIBATOMIC_LIBRARIES NAMES libatomic.so libatomic.so.1)

if (NOT LIBATOMIC_LIBRARIES)
  if (Libatomic_FIND_REQUIRED)
    message(FATAL_ERROR "Could not find libatomic")
  else()
    message(STATUS "Could not find libatomic")
  endif()
else()
  message(STATUS "Found libatomic: ${LIBATOMIC_LIBRARIES}")
endif()
