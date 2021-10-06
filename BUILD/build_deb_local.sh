#!/bin/bash

# do the real building work
# this script is executed on build VM

set -x

cd ./MaxScale

NCPU=$(grep -c processor /proc/cpuinfo)

if [ "$PARALLEL_BUILD" == "no" ]
then
    NCPU=1
fi

mkdir _build
cd _build
cmake ..  $cmake_flags
export LD_LIBRARY_PATH=$PWD/log_manager:$PWD/query_classifier
make || exit 1

if [[ "$cmake_flags" =~ "BUILD_TESTS=Y" ]]
then
    # We don't care about memory leaks in the tests (e.g. servers are never freed)
    export ASAN_OPTIONS=detect_leaks=0
    # All tests must pass otherwise the build is considered a failure
    ctest --timeout 120 --output-on-failure || exit 1
fi

export LD_LIBRARY_PATH=$(for i in `find $PWD/ -name '*.so*'`; do echo $(dirname $i); done|sort|uniq|xargs|sed -e 's/[[:space:]]/:/g')
make -j${NCPU} package
res=$?
if [ $res != 0 ] ; then
        echo "Make package failed"
        exit $res
fi

sudo rm ../CMakeCache.txt
sudo rm CMakeCache.txt

echo "Building tarball..."
cmake .. $cmake_flags -DTARBALL=Y
sudo make -j${NCPU} package


cp _CPack_Packages/Linux/DEB/*.deb ../

rm ../CMakeCache.txt
rm CMakeCache.txt
cd ..
cp _build/*.deb .
cp *.deb ..
cp _build/*.gz .

set -x
if [ "$build_experimental" == "yes" ]
then
    for component in experimental devel cdc-connector
    do
        cd _build
        rm CMakeCache.txt
        export LD_LIBRARY_PATH=""
        cmake ..  $cmake_flags -DTARGET_COMPONENT=$component
        export LD_LIBRARY_PATH=$(for i in `find $PWD/ -name '*.so*'`; do echo $(dirname $i); done|sort|uniq|xargs|sed -e 's/[[:space:]]/:/g')
        make -j${NCPU} package
        cp _CPack_Packages/Linux/DEB/*.deb ../
        cd ..
        cp _build/*.deb .
        cp *.deb ..
        cp _build/*.gz .
    done
fi

sudo dpkg -i ../maxscale*.deb
set +x
