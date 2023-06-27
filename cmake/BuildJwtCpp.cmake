# Downloads jwt-cpp: https://github.com/Thalhammer/jwt-cpp
set(JWT_CPP_REPO "https://github.com/Thalhammer/jwt-cpp.git" CACHE STRING "jwt-cpp git repo")

ExternalProject_Add(jwt-cpp
  GIT_REPOSITORY ${JWT_CPP_REPO}
  GIT_TAG "v0.6.0"
  GIT_SHALLOW TRUE
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  UPDATE_COMMAND ""
  INSTALL_COMMAND ""
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)
include_directories(${CMAKE_BINARY_DIR}/jwt-cpp-prefix/src/jwt-cpp/include/)
