# Locates the libssh library
#
# The following variables are set:
# LIBSSH_FOUND - If the libssh library was found
# LIBSSH_LIBRARIES - Path to the library files
# LIBSSH_INCLUDE_DIR - Path to directory with the headers

find_library(LIBSSH_LIBRARY ssh)
find_path(LIBSSH_INCLUDE_DIR libssh.h PATH_SUFFIXES libssh)

if (LIBSSH_LIBRARY AND LIBSSH_INCLUDE_DIR)
  set(LIBSSH_FOUND TRUE)
  message(STATUS "Found libssh: ${LIBSSH_LIBRARY}")
elseif(LibSSH_FIND_REQUIRED)
  message(FATAL_ERROR "Could not find libssh")
else()
  message(STATUS "Could not find libssh")
endif()
