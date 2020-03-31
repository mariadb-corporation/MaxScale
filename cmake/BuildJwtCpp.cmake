# Downloads jwt-cpp: https://github.com/Thalhammer/jwt-cpp
ExternalProject_Add(jwt-cpp
  URL "https://github.com/Thalhammer/jwt-cpp/archive/v0.3.1.tar.gz"
  SOURCE_DIR ${CMAKE_BINARY_DIR}/jwt-cpp/
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)
include_directories(${CMAKE_BINARY_DIR}/jwt-cpp/include/)
