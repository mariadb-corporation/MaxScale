#!/bin/bash

# Common steps that both DEB and RPM builds perform.

set -x

function is_ppc64() {
    [ "$(arch)" == "ppc64le" ]
}


cd ./MaxScale || exit 1
git submodule update --init
cd ..

NCPU=$(grep -c processor /proc/cpuinfo)
NCPUSTR="-j${NCPU}"

if [ "$PARALLEL_BUILD" == "no" ] || [ is_ppc64 ]
then
    NCPUSTR=""
fi

mkdir _build
cd _build || exit 1
cmake ../MaxScale -DCMAKE_COLOR_MAKEFILE=N $cmake_flags
make ${NCPUSTR} || exit 1

if [[ "$cmake_flags" =~ "BUILD_TESTS=Y" ]]
then
    # We don't care about memory leaks in the tests (e.g. servers are never freed)
    export ASAN_OPTIONS=detect_leaks=0
    export UBSAN_OPTIONS=abort_on_error=1
    # All tests must pass otherwise the build is considered a failure
    ctest --timeout 120 --output-on-failure "-j${NCPU}" || exit 1
fi

sudo make ${NCPUSTR} package
res=$?
if [ $res != 0 ] ; then
	echo "Make package failed"
	exit $res
fi

sudo rm CMakeCache.txt

echo "Building tarball..."
cmake ../MaxScale $cmake_flags -DTARBALL=Y
sudo make ${NCPUSTR} package
