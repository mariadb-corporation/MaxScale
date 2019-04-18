#!/bin/bash

# cmake
wget -q http://max-tst-01.mariadb.com/ci-repository/cmake-3.7.1-Linux-x86_64.tar.gz --no-check-certificate
if [ $? != 0 ] ; then
    echo "CMake can not be downloaded from Maxscale build server, trying from cmake.org"
    wget -q https://cmake.org/files/v3.7/cmake-3.7.1-Linux-x86_64.tar.gz --no-check-certificate
fi
sudo tar xzf cmake-3.7.1-Linux-x86_64.tar.gz -C /usr/ --strip-components=1

cmake_version=`cmake --version | grep "cmake version" | awk '{ print $3 }'`
if [ "`echo -e "3.7.1\n$cmake_version"|sort -V|head -n 1`" != "3.7.1" ] ; then
    echo "cmake does not work! Trying to build from source"
    wget -q https://cmake.org/files/v3.7/cmake-3.7.1.tar.gz --no-check-certificate
    tar xzf cmake-3.7.1.tar.gz
    cd cmake-3.7.1

    ./bootstrap
    gmake
    sudo make install
    cd ..
fi

