set(LIBKMIP_REPO "https://github.com/OpenKMIP/libkmip.git" CACHE INTERNAL "libkmip git repository")
set(LIBKMIP_TAG "v0.2.0" CACHE INTERNAL "libkmip tag")

ExternalProject_Add(libkmip
  GIT_REPOSITORY ${LIBKMIP_REPO}
  GIT_TAG ${LIBKMIP_TAG}
  GIT_SHALLOW TRUE
  SOURCE_DIR ${CMAKE_BINARY_DIR}/libkmip
  BINARY_DIR ${CMAKE_BINARY_DIR}/libkmip
  CONFIGURE_COMMAND ""
  # The tests and demos are unconditionally built and they don't support the OpenSSL version that's
  # installed in CentOS 7. Remove them both as we only need the libraries.
  PATCH_COMMAND sed -i "s/all: demos tests/all: /" ${CMAKE_BINARY_DIR}/libkmip/Makefile
  # The make command fails with an error about .so files if it has already been installed once.
  # To prevent this, run the install with the -i option to ignore it. It also doesn't build the
  # static library with -fPIC so the CFLAGS must be defined here.
  BUILD_COMMAND make "CFLAGS=-fPIC -std=c11"
  INSTALL_COMMAND make -i PREFIX=${CMAKE_BINARY_DIR}/libkmip/install install
  UPDATE_COMMAND ""
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1
  )

set(LIBKMIP_FOUND TRUE CACHE INTERNAL "")
set(LIBKMIP_INCLUDE_DIR ${CMAKE_BINARY_DIR}/libkmip/install/include/ CACHE INTERNAL "")
set(LIBKMIP_LIBRARIES ${CMAKE_BINARY_DIR}/libkmip/install/lib/libkmip.a CACHE INTERNAL "")
