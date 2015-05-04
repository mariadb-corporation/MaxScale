# this CMake file defines the following variables
# JEMALLOC_FOUND - Jemalloc was found
# JEMALLOC_LIBRARIES - Jemalloc library
find_library(JEMALLOC_LIBRARIES NAMES jemalloc libjemalloc.so.4 libjemalloc.so.4.2.2)
if(JEMALLOC_LIBRARIES)
    set(JEMALLOC_FOUND TRUE CACHE INTERNAL "")
    message(STATUS "Found libjemalloc: ${JEMALLOC_LIBRARIES}")
else()
  set(JEMALLOC_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "Could not find libjemalloc, using system default malloc instead.")
endif()
