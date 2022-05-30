set(LIBSSH_REPO "https://git.libssh.org/projects/libssh.git" CACHE STRING "libssh git repository")
set(LIBSSH_TAG "libssh-0.9.6" CACHE STRING "libssh git tag")

set(LIBSSH_INSTALL_DIR ${CMAKE_BINARY_DIR}/libssh/install)

# LibSSH is LGPL.
ExternalProject_Add(libssh
        GIT_REPOSITORY ${LIBSSH_REPO}
        GIT_TAG ${LIBSSH_TAG}
        GIT_SHALLOW TRUE
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${LIBSSH_INSTALL_DIR} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_C_FLAGS=-fPIC -DBUILD_SHARED_LIBS=N -DWITH_GSSAPI=N
        SOURCE_DIR ${CMAKE_BINARY_DIR}/libssh/source
        BINARY_DIR ${CMAKE_BINARY_DIR}/libssh/build
        INSTALL_DIR ${LIBSSH_INSTALL_DIR}
        UPDATE_COMMAND ""
        LOG_DOWNLOAD 1
        LOG_UPDATE 1
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1)

set(LIBSSH_FOUND TRUE)
set(LIBSSH_LIBRARY ${LIBSSH_INSTALL_DIR}/${CMAKE_INSTALL_LIBDIR}/libssh.a)
set(LIBSSH_INCLUDE_DIR ${LIBSSH_INSTALL_DIR}/include)
