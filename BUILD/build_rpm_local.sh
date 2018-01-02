#!/bin/bash

# do the real building work
# this script is executed on build VM

set -x

cd ./MaxScale

mkdir _build
cd _build
cmake ..  $cmake_flags
make

if [[ "$cmake_flags" =~ "BUILD_TESTS" ]]
then
    # All tests must pass otherwise the build is considered a failure
    ctest --output-on-failure || exit 1
fi

if [ $remove_strip == "yes" ] ; then
	sudo rm -rf /usr/bin/strip
	sudo touch /usr/bin/strip
	sudo chmod a+x /usr/bin/strip
fi
sudo make package
res=$?
if [ $res != 0 ] ; then
	echo "Make package failed"
	exit $res
fi

sudo rm ../CMakeCache.txt
sudo rm CMakeCache.txt

echo "Building tarball..."
cmake .. $cmake_flags -DTARBALL=Y
sudo make package

cd ..
cp _build/*.rpm .
cp _build/*.gz .

if [ "$build_experimental" == "yes" ] ; then
        sudo rm -rf _build
        mkdir _build
        cd _build
        cmake ..  $cmake_flags -DTARGET_COMPONENT=experimental
        sudo make package
        cd ..
        cp _build/*.rpm .
	cp _build/*.gz .

        sudo rm -rf _build
        mkdir _build
        cd _build
        cmake ..  $cmake_flags -DTARGET_COMPONENT=devel
        sudo make package
        cd ..
        cp _build/*.rpm .
	cp _build/*.gz .
fi

if [ "$BUILD_RABBITMQ" == "yes" ] ; then
  cmake ../rabbitmq_consumer/  $cmake_flags
  sudo make package
  res=$?
  if [ $res != 0 ] ; then
        exit $res
  fi
  cd ..
  cp _build/*.rpm .
  cp _build/*.gz .
fi

sudo rpm -i maxscale*.rpm
