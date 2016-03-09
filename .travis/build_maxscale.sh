#!/bin/bash


# these environment variables are set in .travis.yml
# MARIADB_URL=https://downloads.mariadb.org/interstitial/mariadb-5.5.48/bintar-linux-glibc_214-x86_64/mariadb-5.5.48\-linux-glibc_214-x86_64.tar.gz/from/http%3A//mirror.netinch.com/pub/mariadb/
# MARIADB_TAR=mariadb-5.5.48-linux-glibc_214-x86_64.tar.gz
# MARIADB_DIR=mariadb-5.5.48-linux-x86_64


echo TRAVIS_BUILD_DIR: ${TRAVIS_BUILD_DIR}
echo MARIADB_DIR: ${MARIADB_DIR}

mkdir build
cd build

cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DMYSQL_EMBEDDED_INCLUDE_DIR=${TRAVIS_BUILD_DIR}/${MARIADB_DIR}/include/ -DMYSQL_EMBEDDED_LIBRARIES=${TRAVIS_BUILD_DIR}/${MARIADB_DIR}/lib/libmysqld.a -DERRMSG=${TRAVIS_BUILD_DIR}/${MARIADB_DIR}/share/english/errmsg.sys

make VERBOSE=1
sudo make install
sudo make testcore

sudo ./postinst
maxscale --version
