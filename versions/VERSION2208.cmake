# MaxScale version for CMake
#
# This file contains cache values for CMake which control MaxScale's version
# number.

set(MAXSCALE_VERSION_MAJOR "22" CACHE STRING "Major version")
set(MAXSCALE_VERSION_MINOR "08" CACHE STRING "Minor version")
set(MAXSCALE_VERSION_PATCH "11" CACHE STRING "Patch version")

# Used in version.hh.in, no leading 0.
set(MAXSCALE_VERSION_MINOR_NUM "8" CACHE STRING "Minor version")

# This should only be incremented if a package is rebuilt
set(MAXSCALE_BUILD_NUMBER 1 CACHE STRING "Release number")

set(MAXSCALE_MATURITY "GA" CACHE STRING "Release maturity")

set(MAXSCALE_VERSION "${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}.${MAXSCALE_VERSION_PATCH}")
