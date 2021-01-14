# Tarball package configuration
message(STATUS "Generating tar.gz packages")
set(MAXSCALE_BINDIR /bin CACHE PATH "" FORCE)
set(MAXSCALE_LIBDIR /lib/maxscale CACHE PATH "" FORCE)
set(MAXSCALE_SHAREDIR /share CACHE PATH "" FORCE)
set(MAXSCALE_DOCDIR /share CACHE PATH "" FORCE)
set(MAXSCALE_VARDIR /var CACHE PATH "" FORCE)
set(MAXSCALE_CONFDIR /etc CACHE PATH "" FORCE)
set(CMAKE_INSTALL_PREFIX "/" CACHE PATH "" FORCE)
set(CMAKE_INSTALL_DATADIR /share CACHE PATH "" FORCE)
set(DEFAULT_LIB_SUBPATH /lib/maxscale CACHE PATH "" FORCE)
set(DEFAULT_LIBDIR "/usr/local/maxscale/lib/maxscale" CACHE PATH "" FORCE)
set(CPACK_GENERATOR "TGZ")

# Include the var directories in the tarball
#
# On some platforms with certain CMake versions, installing empty directories
# with tarballs does not work. As a workaround, the .cmake-tgz-workaround file
# is installed into the would-be empty directories.
file(WRITE ${CMAKE_BINARY_DIR}/.cmake-tgz-workaround "")
install(FILES ${CMAKE_BINARY_DIR}/.cmake-tgz-workaround DESTINATION var/cache/maxscale)
install(FILES ${CMAKE_BINARY_DIR}/.cmake-tgz-workaround DESTINATION var/log/maxscale)
install(FILES ${CMAKE_BINARY_DIR}/.cmake-tgz-workaround DESTINATION var/run/maxscale)
install(FILES ${CMAKE_BINARY_DIR}/.cmake-tgz-workaround DESTINATION var/lib/maxscale)
install(FILES ${CMAKE_BINARY_DIR}/.cmake-tgz-workaround DESTINATION var/lib/maxscale/maxscale.cnf.d)
install(FILES ${CMAKE_BINARY_DIR}/.cmake-tgz-workaround DESTINATION etc/maxscale.modules.d)
install(FILES ${CMAKE_BINARY_DIR}/.cmake-tgz-workaround DESTINATION var/lib/plugin)
install(FILES ${CMAKE_BINARY_DIR}/.cmake-tgz-workaround DESTINATION ${DEFAULT_CONNECTOR_PLUGIN_SUBPATH})

if(DISTRIB_SUFFIX)
  set(CPACK_PACKAGE_FILE_NAME "maxscale-${MAXSCALE_VERSION}.${DISTRIB_SUFFIX}")
else()
  set(CPACK_PACKAGE_FILE_NAME "maxscale-${MAXSCALE_VERSION}")
endif()
