# This CMake-file locates PAM libraries on the system
#
# The following variables are set:
# PAM_FOUND - If the PAM library was found
# PAM_LIBRARIES - Path to the library
# PAM_INCLUDE_DIR - Path to headers

find_path(PAM_INCLUDE_DIR pam_appl.h PATH_SUFFIXES security)
find_library(PAM_LIBRARIES NAMES pam)
message(STATUS "Found PAM include dirs: ${PAM_INCLUDE_DIR}")
if (PAM_INCLUDE_DIR AND PAM_LIBRARIES)
  message(STATUS "Found PAM: ${PAM_LIBRARIES}")
  set(PAM_FOUND TRUE BOOL)
else()
  message(STATUS "PAM libraries not found")
  set(PAM_FOUND FALSE BOOL)
endif()


