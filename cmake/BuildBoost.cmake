# This file makes the bundled Boost headers available in the source directory
# where it is included from. To use it in a module, use:
#
#   include(${CMAKE_SOURCE_DIR}/cmake/BuildBoost.cmake)

FetchContent_GetProperties(boost)
if(NOT boost_POPULATED)
  # Extractes the bundled Boost library
  FetchContent_Declare(boost
    URL "${CMAKE_SOURCE_DIR}/boost/boost-1.76.0.tar.gz"
    SOURCE_DIR ${CMAKE_BINARY_DIR}/boost/
    BINARY_DIR ${CMAKE_BINARY_DIR}/boost/
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    LOG_DOWNLOAD 1
    LOG_UPDATE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1)
  FetchContent_Populate(boost)
endif()

# Make sure we include the custom headers before the system boost headers
include_directories(BEFORE ${CMAKE_BINARY_DIR}/boost/)

# Also include the MaxScale utility headers
include_directories(${CMAKE_SOURCE_DIR}/boost/include/)
