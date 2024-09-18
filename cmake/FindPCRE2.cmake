# This CMake file tries to find the Perl regular expression libraries
# The following variables are set:
# PCRE2_FOUND - System has the PCRE library
# PCRE2_LIBRARIES - The PCRE library file
# PCRE2_INCLUDE_DIRS - The folder with the PCRE headers
# PCRE2_VERSION - The version of the found library

find_library(PCRE2_LIBRARIES NAMES pcre2 pcre2-8)
find_path(PCRE2_INCLUDE_DIRS pcre2.h)
if(PCRE2_LIBRARIES AND PCRE2_INCLUDE_DIRS)
  # TODO: This reads the file twice, not very efficient
  file(STRINGS "${PCRE2_INCLUDE_DIRS}/pcre2.h" PCRE2_MAJOR_VERSION REGEX "#define *PCRE2_MAJOR")
  file(STRINGS "${PCRE2_INCLUDE_DIRS}/pcre2.h" PCRE2_MINOR_VERSION REGEX "#define *PCRE2_MINOR")
  string(REGEX REPLACE "#define *PCRE2_MAJOR *" "" PCRE2_MAJOR_VERSION "${PCRE2_MAJOR_VERSION}")
  string(REGEX REPLACE "#define *PCRE2_MINOR *" "" PCRE2_MINOR_VERSION "${PCRE2_MINOR_VERSION}")
  set(PCRE2_VERSION "${PCRE2_MAJOR_VERSION}.${PCRE2_MINOR_VERSION}")

  message(STATUS "Found pcre2 version ${PCRE2_VERSION}: ${PCRE2_LIBRARIES}")
  set(PCRE2_FOUND TRUE CACHE BOOL "Found PCRE2 libraries" FORCE)
  add_custom_target(pcre2)
else()
  set(PCRE2_FOUND FALSE CACHE BOOL "Found PCRE2 libraries" FORCE)
  message(STATUS "PCRE2 library not found.")
endif()
