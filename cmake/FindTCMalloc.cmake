# this CMake file defines the following variables
# TCMALLOC_FOUND - TCMalloc was found
# TCMALLOC_LIBRARIES - TCMalloc library
find_library(TCMALLOC_LIBRARIES NAMES tcmalloc libtcmalloc.so.4 libtcmalloc.so.4.2.2)
if(TCMALLOC_LIBRARIES)
    set(TCMALLOC_FOUND TRUE CACHE INTERNAL "")
    message(STATUS "Found libtcmalloc: ${TCMALLOC_LIBRARIES}")
else()
  set(TCMALLOC_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "Could not find libtcmalloc, using system default malloc instead.")
endif()
