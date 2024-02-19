# Common packaging configuration

execute_process(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE CPACK_PACKAGE_ARCHITECTURE)

if (NOT INSTALL_EXPERIMENTAL)
  get_cmake_property(CPACK_COMPONENTS_ALL COMPONENTS)
  list(REMOVE_ITEM CPACK_COMPONENTS_ALL "experimental")
endif()

# Generic CPack configuration variables
set(CPACK_SET_DESTDIR ON)
set(CPACK_PACKAGE_RELOCATABLE FALSE)
set(CPACK_STRIP_FILES FALSE)
set(CPACK_PACKAGE_VERSION_MAJOR "${MAXSCALE_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${MAXSCALE_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${MAXSCALE_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "MariaDB plc")
set(CPACK_PACKAGE_VENDOR "MariaDB plc")
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Descriptions for the main packages
set(CORE_PACKAGE_SUMMARY "MaxScale - An intelligent database proxy")
set(CORE_PACKAGE_DESCRIPTION "
MariaDB MaxScale is an intelligent proxy that allows forwarding of
database statements to one or more database servers using complex rules,
a semantic understanding of the database statements and the roles of
the various servers within the backend cluster of databases.

MaxScale is designed to provide load balancing and high availability
functionality transparently to the applications. In addition it provides
a highly scalable and flexible architecture, with plugin components to
support different protocols and routing decisions.")

set(EXPERIMENTAL_PACKAGE_SUMMARY "MaxScale experimental modules")
set(EXPERIMENTAL_PACKAGE_DESCRIPTION "
This package contains experimental and community contributed modules for MariaDB
MaxScale. The packages are not fully supported parts of MaxScale and should be
considered as alpha quality software.")

# If we're building something other than the main package, append the target name
# to the package name.

set(CPACK_PACKAGE_NAME "${PACKAGE_NAME}")

# See if we are on a RPM-capable or DEB-capable system
find_program(RPMBUILD rpmbuild)
find_program(DEBBUILD dpkg-buildpackage)

option(TARBALL "Build a tarball package" OFF)

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
  message(STATUS "To remove these installed files, run the 'prerm' shell script located in the same folder.")
endif()
