# RPM specific CPack configuration parameters

set(CPACK_GENERATOR "${CPACK_GENERATOR};RPM")
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_RPM_PACKAGE_VENDOR "MariaDB plc")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION "/etc /etc/ld.so.conf.d /etc/init.d /etc/rc.d/init.d /usr/share/man /usr/share/man1")
set(CPACK_RPM_SPEC_MORE_DEFINE "%define ignore \#")
set(CPACK_RPM_SPEC_INSTALL_POST "/bin/true")

if(DISTRIB_SUFFIX)
  set(VERSION_SUFFIX "${MAXSCALE_VERSION}-${MAXSCALE_BUILD_NUMBER}.${DISTRIB_SUFFIX}.${CMAKE_SYSTEM_PROCESSOR}.rpm")
  set(CPACK_RPM_PACKAGE_RELEASE "${MAXSCALE_BUILD_NUMBER}.${DISTRIB_SUFFIX}")
else()
  set(VERSION_SUFFIX "${MAXSCALE_VERSION}-${MAXSCALE_BUILD_NUMBER}.${CMAKE_SYSTEM_PROCESSOR}.rpm")
  set(CPACK_RPM_PACKAGE_RELEASE ${MAXSCALE_BUILD_NUMBER})
endif()

# This prevents the default %post from running which causes binaries to be
# striped. Without this, MaxCtrl will not work on all systems as the
# binaries will be stripped.
set(CPACK_RPM_SPEC_INSTALL_POST "/bin/true")

set(CPACK_RPM_PACKAGE_LICENSE "MariaDB BSL 1.1")

set(IGNORED_DIRS
  "%ignore /etc"
  "%ignore /etc/init.d"
  "%ignore /etc/ld.so.conf.d"
  "%ignore ${CMAKE_INSTALL_PREFIX}"
  "%ignore ${CMAKE_INSTALL_PREFIX}/bin"
  "%ignore ${CMAKE_INSTALL_PREFIX}/include"
  "%ignore ${CMAKE_INSTALL_PREFIX}/lib"
  "%ignore ${CMAKE_INSTALL_PREFIX}/lib64"
  "%ignore ${CMAKE_INSTALL_PREFIX}/share"
  "%ignore ${CMAKE_INSTALL_PREFIX}/share/man"
  "%ignore ${CMAKE_INSTALL_PREFIX}/share/man/man1")

set(CPACK_RPM_USER_FILELIST "${IGNORED_DIRS}")

#
# Core MaxScale package
#
set(CPACK_RPM_CORE_PACKAGE_NAME "maxscale")
set(CPACK_RPM_CORE_FILE_NAME "maxscale-${VERSION_SUFFIX}")
set(CPACK_RPM_CORE_PACKAGE_SUMMARY "${CORE_PACKAGE_SUMMARY}")
set(CPACK_RPM_CORE_PACKAGE_DESCRIPTION "${CORE_PACKAGE_DESCRIPTION}")
# Include the post-install scripts only in the core component
set(CPACK_RPM_CORE_POST_INSTALL_SCRIPT_FILE ${CMAKE_BINARY_DIR}/postinst)
set(CPACK_RPM_CORE_PRE_UNINSTALL_SCRIPT_FILE ${CMAKE_BINARY_DIR}/prerm)

#
# Experimental package (not built by default)
#
set(CPACK_RPM_EXPERIMENTAL_PACKAGE_NAME "maxscale-experimental")
set(CPACK_RPM_EXPERIMENTAL_FILE_NAME "maxscale-experimental-${VERSION_SUFFIX}")
set(CPACK_RPM_EXPERIMENTAL_PACKAGE_SUMMARY "${EXPERIMENTAL_PACKAGE_SUMMARY}")
set(CPACK_RPM_EXPERIMENTAL_PACKAGE_DESCRIPTION "${EXPERIMENTAL_PACKAGE_DESCRIPTION}")

message(STATUS "Generating RPM packages")
