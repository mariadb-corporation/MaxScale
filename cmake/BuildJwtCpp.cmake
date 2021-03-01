# Downloads jwt-cpp: https://github.com/Thalhammer/jwt-cpp
ExternalProject_Add(jwt-cpp
  GIT_REPOSITORY "https://github.com/Thalhammer/jwt-cpp.git"
  GIT_TAG "v0.4.0"
  GIT_SHALLOW TRUE
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  UPDATE_COMMAND ""
  PATCH_COMMAND cd ${CMAKE_BINARY_DIR}/jwt-cpp-prefix/src/jwt-cpp/ && git checkout -f && git apply ${CMAKE_SOURCE_DIR}/cmake/jwt-cpp.patch
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)
include_directories(${CMAKE_BINARY_DIR}/jwt-cpp-prefix/src/jwt-cpp/include/)
