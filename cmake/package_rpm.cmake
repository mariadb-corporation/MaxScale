# RPM specific CPack configuration parameters

set(CPACK_GENERATOR "${CPACK_GENERATOR};RPM")
set(CPACK_RPM_PACKAGE_RELEASE ${MAXSCALE_BUILD_NUMBER})
set(CPACK_RPM_PACKAGE_VENDOR "MariaDB Corporation Ab")
set(CPACK_RPM_PACKAGE_LICENSE "MariaDB BSL")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION "/etc /etc/ld.so.conf.d /etc/init.d /etc/rc.d/init.d /usr/share/man /usr/share/man1")
set(CPACK_RPM_SPEC_MORE_DEFINE "%define ignore \#")
set(CPACK_RPM_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_RPM_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}")

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

if(TARGET_COMPONENT STREQUAL "core")
  set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE ${CMAKE_BINARY_DIR}/postinst)
  set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE ${CMAKE_BINARY_DIR}/postrm)

  # Installing this prevents RPM from deleting the /var/lib/maxscale folder
  install(DIRECTORY DESTINATION ${MAXSCALE_VARDIR}/lib/maxscale)
elseif(TARGET_COMPONENT STREQUAL "devel")
  set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}\n${DESCRIPTION_TEXT}")
endif()

if(EXTRA_PACKAGE_DEPENDENCIES)
  set(CPACK_RPM_PACKAGE_REQUIRES "${EXTRA_PACKAGE_DEPENDENCIES}")
endif()

message(STATUS "Generating RPM packages")
