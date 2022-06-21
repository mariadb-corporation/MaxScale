include(GNUInstallDirs)
include(CheckIncludeFileCXX)
CHECK_INCLUDE_FILE_CXX(filesystem HAVE_CXX_FILESYSTEM CMAKE_REQUIRED_FLAGS -std=c++17)

if (HAVE_CXX_FILESYSTEM)
  set(LIBVAULT_REPO "https://github.com/abedra/libvault.git" CACHE INTERNAL "libvault git repository")
  set(LIBVAULT_TAG "0.51.0" CACHE INTERNAL "libvault tag")

  ExternalProject_Add(libvault
    GIT_REPOSITORY ${LIBVAULT_REPO}
    GIT_TAG ${LIBVAULT_TAG}
    GIT_SHALLOW TRUE
    INSTALL_DIR ${CMAKE_BINARY_DIR}/libvault/install
    CMAKE_ARGS -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/libvault/install -DENABLE_TEST=N -DBUILD_SHARED_LIBS=N -DCMAKE_CXX_FLAGS=-fPIC
    UPDATE_COMMAND ""
    LOG_DOWNLOAD 1
    LOG_UPDATE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    )

  set(LIBVAULT_FOUND TRUE CACHE INTERNAL "")
  set(LIBVAULT_INCLUDE_DIR ${CMAKE_BINARY_DIR}/libvault/install/${CMAKE_INSTALL_INCLUDEDIR}/ CACHE INTERNAL "")
  set(LIBVAULT_LIBRARIES ${CMAKE_BINARY_DIR}/libvault/install/lib/libvault.a CACHE INTERNAL "")

  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    # GCC 8 has the <filesystem> header but doesn't automatically link against the required
    # libraries. This was, for whatever reason, only added in GCC9.
    set(LIBVAULT_LIBRARIES ${LIBVAULT_LIBRARIES} stdc++fs CACHE INTERNAL "")
  endif()
else()
  set(LIBVAULT_FOUND FALSE CACHE INTERNAL "")
endif()
