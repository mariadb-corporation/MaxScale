# MaxScale version for CMake
#
# This file contains cache values for CMake which control MaxScale's version
# number.

set(MAXSCALE_VERSION_MAJOR "2" CACHE STRING "Major version")
set(MAXSCALE_VERSION_MINOR "1" CACHE STRING "Minor version")
set(MAXSCALE_VERSION_PATCH "13" CACHE STRING "Patch version")

# This should only be incremented if a package is rebuilt
set(MAXSCALE_BUILD_NUMBER 1 CACHE STRING "Release number")

set(MAXSCALE_VERSION_NUMERIC "${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}.${MAXSCALE_VERSION_PATCH}")
set(MAXSCALE_VERSION "${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}.${MAXSCALE_VERSION_PATCH}")
