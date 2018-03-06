#!/bin/bash


# these environment variables are set in .travis.yml
# MARIADB_URL=https://downloads.mariadb.org/interstitial/mariadb-5.5.48/bintar-linux-glibc_214-x86_64/mariadb-5.5.48\-linux-glibc_214-x86_64.tar.gz/from/http%3A//mirror.netinch.com/pub/mariadb/
# MARIADB_TAR=mariadb-5.5.48-linux-glibc_214-x86_64.tar.gz
# MARIADB_DIR=mariadb-5.5.48-linux-x86_64


echo TRAVIS_BUILD_DIR: ${TRAVIS_BUILD_DIR}

# Configure the build environment
./BUILD/install_build_deps.sh

mkdir build
cd build

cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=Y

make
make test || exit 1

sudo make install
sudo ./postinst
maxscale --version
