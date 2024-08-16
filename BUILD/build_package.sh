#!/bin/bash

# Common steps that both DEB and RPM builds perform.

set -x

cd ./MaxScale || exit 1

git submodule update --init

NCPU=$(grep -c processor /proc/cpuinfo)

if [ "$PARALLEL_BUILD" == "no" ]
then
    NCPU=1
fi

mkdir _build
cd _build || exit 1
cmake ..  $cmake_flags
make "-j${NCPU}" || exit 1

if [[ "$cmake_flags" =~ "BUILD_TESTS=Y" ]]
then
    # We don't care about memory leaks in the tests (e.g. servers are never freed)
    export ASAN_OPTIONS=detect_leaks=0
    # All tests must pass otherwise the build is considered a failure
    ctest --timeout 120 --output-on-failure "-j${NCPU}" || exit 1
fi

sudo make "-j${NCPU}" package
res=$?
if [ $res != 0 ] ; then
	echo "Make package failed"
	exit $res
fi

sudo rm ../CMakeCache.txt
sudo rm CMakeCache.txt

echo "Building tarball..."
cmake .. $cmake_flags -DTARBALL=Y
sudo make "-j${NCPU}" package
