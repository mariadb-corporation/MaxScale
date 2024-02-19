# DEB specific CPack configuration parameters
set(CPACK_GENERATOR "${CPACK_GENERATOR};DEB")
execute_process(COMMAND lsb_release -cs OUTPUT_VARIABLE DEB_CODENAME OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE DEB_ARCHITECTURE OUTPUT_STRIP_TRAILING_WHITESPACE)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${DEB_ARCHITECTURE})
set(CPACK_DEBIAN_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}~${DEB_CODENAME}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "${MAXSCALE_BUILD_NUMBER}")

# The DEB-DEFAULT value in CPACK_DEBIAN_<COMPONENT>_FILE_NAME makes it so that
# CPack will correctly form the file name from
# CPACK_DEBIAN_<COMPONENT>_PACKAGE_NAME and the version information that's set.

#
# Core MaxScale package
#
set(CPACK_DEBIAN_CORE_PACKAGE_NAME "maxscale")
set(CPACK_DEBIAN_CORE_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_CORE_PACKAGE_CONTROL_EXTRA "${CMAKE_BINARY_DIR}/postinst;${CMAKE_BINARY_DIR}/prerm")
set(CPACK_DEBIAN_CORE_PACKAGE_SHLIBDEPS ON)

# Some modules were moved from maxscale-experimental into maxscale in 6. The
# version 6 modules must replace the 2.5 ones.
set(CPACK_DEBIAN_CORE_PACKAGE_REPLACES "maxscale-experimental (<< 6)")

#
# Experimental package (not built by default)
#
set(CPACK_DEBIAN_EXPERIMENTAL_PACKAGE_NAME "maxscale-experimental")
set(CPACK_DEBIAN_EXPERIMENTAL_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_EXPERIMENTAL_PACKAGE_DEPENDS "maxscale")

message(STATUS "Generating DEB packages for ${DEB_ARCHITECTURE}")
