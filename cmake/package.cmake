# Common packaging configuration

execute_process(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE CPACK_PACKAGE_ARCHITECTURE)

# Check target
set(PACK_TARGETS "core" "devel" "external" "all")
if(DEFINED TARGET_COMPONENT AND NOT TARGET_COMPONENT STREQUAL "")
  set(LIST_INDEX -1)
  list(FIND PACK_TARGETS ${TARGET_COMPONENT} LIST_INDEX)
  if (${LIST_INDEX} EQUAL -1)
    message(FATAL_ERROR "Unrecognized TARGET_COMPONENT value. Allowed values: ${PACK_TARGETS}.")
  endif()
else()
  set(TARGET_COMPONENT "core")
  message(STATUS "No TARGET_COMPONENT defined, using default value 'core'")
endif()

# Generic CPack configuration variables
set(CPACK_SET_DESTDIR ON)
set(CPACK_PACKAGE_RELOCATABLE FALSE)
set(CPACK_STRIP_FILES FALSE)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MaxScale - The Dynamic Data Routing Platform")
set(CPACK_PACKAGE_VERSION_MAJOR "${MAXSCALE_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${MAXSCALE_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${MAXSCALE_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "MariaDB Corporation Ab")
set(CPACK_PACKAGE_VENDOR "MariaDB Corporation Ab")
set(CPACK_PACKAGE_DESCRIPTION_FILE ${CMAKE_SOURCE_DIR}/etc/DESCRIPTION)
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# If building devel package, change the description. Deb- and rpm-specific parameters are set in their
# dedicated files "package_(deb/rpm).cmake"
if (TARGET_COMPONENT STREQUAL "devel")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MaxScale plugin development headers")
  set(DESCRIPTION_TEXT "\
 This package contains header files required for plugin module development for MariaDB MaxScale. \
The source of MariaDB MaxScale is not required.")
endif()

# If we're building something other than the main package, append the target name
# to the package name.
if(DEFINED TARGET_COMPONENT AND NOT TARGET_COMPONENT STREQUAL "core" AND NOT TARGET_COMPONENT STREQUAL "")
  set(CPACK_PACKAGE_NAME "${PACKAGE_NAME}-${TARGET_COMPONENT}")
else()
  set(CPACK_PACKAGE_NAME "${PACKAGE_NAME}")
endif()

if(DISTRIB_SUFFIX)
  set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${MAXSCALE_VERSION}-${MAXSCALE_BUILD_NUMBER}.${DISTRIB_SUFFIX}.${CMAKE_SYSTEM_PROCESSOR}")
else()
  set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${MAXSCALE_VERSION}-${MAXSCALE_BUILD_NUMBER}.${CMAKE_SYSTEM_PROCESSOR}")
endif()

# See if we are on a RPM-capable or DEB-capable system
find_program(RPMBUILD rpmbuild)
find_program(DEBBUILD dpkg-buildpackage)

if(TARBALL)
  include(cmake/package_tgz.cmake)

elseif(${RPMBUILD} MATCHES "NOTFOUND" AND ${DEBBUILD} MATCHES "NOTFOUND")
  message(FATAL_ERROR "Could not automatically resolve the package generator and no generators "
    "defined on the command line. Please install distribution specific packaging software or "
    "define -DTARBALL=Y to build tar.gz packages.")
else()

  if(${DEBBUILD} MATCHES "NOTFOUND")
    # No DEB packaging tools found, must be an RPM system
    include(cmake/package_rpm.cmake)
  else()
    # We have DEB packaging tools, must be a DEB system
    if (NOT ${RPMBUILD} MATCHES "NOTFOUND")
      # Debian based systems can have both RPM and DEB packaging tools
      message(WARNING "Found both DEB and RPM packaging tools, generating DEB packages. If this is not correct, "
        "remove the packaging tools for the package type you DO NOT want to create.")
    endif()
    include(cmake/package_deb.cmake)
  endif()

  message(STATUS "You can install startup scripts and system configuration files for MaxScale by running the 'postinst' shell script located at ${CMAKE_INSTALL_PREFIX}.")
  message(STATUS "To remove these installed files, run the 'postrm' shell script located in the same folder.")
endif()
