# This CMake file locates the Jansson libraries and headers
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
  set(PAM_FOUND TRUE)
else()
  message(STATUS "Could not find PAM")
endif()

if (PAM_FOUND AND SQLITE_FOUND)
    #include_directories(${PAM_INCLUDE_DIR})
    include_directories(${SQLITE_INCLUDE_DIR})
    add_subdirectory(PAMAuth)
    add_subdirectory(PAMBackendAuth)
else()
    message(STATUS "No PAM libraries or SQLite found, not building PAM authenticator. Current SQLite version is #${SQLITE_VERSION}")
endif()
