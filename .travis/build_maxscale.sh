#!/bin/bash


# these environment variables are set in .travis.yml
# MARIADB_URL=https://downloads.mariadb.org/interstitial/mariadb-5.5.48/bintar-linux-glibc_214-x86_64/mariadb-5.5.48\-linux-glibc_214-x86_64.tar.gz/from/http%3A//mirror.netinch.com/pub/mariadb/
# MARIADB_TAR=mariadb-5.5.48-linux-glibc_214-x86_64.tar.gz
# MARIADB_DIR=mariadb-5.5.48-linux-x86_64


echo TRAVIS_BUILD_DIR: ${TRAVIS_BUILD_DIR}

mkdir build
cd build

cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTS=Y

make VERBOSE=1
make test
sudo make install

sudo ./postinst
maxscale --version
