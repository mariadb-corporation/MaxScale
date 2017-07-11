#!/bin/bash

# This script builds and installs MaxScale, starts a MaxScale instance, runs the
# tests use npm and stops MaxScale.
#
# This is definitely not the most efficient way to test the binaries but it's a
# guaranteed method of creating a consistent and "safe" testing environment.
#
# TODO: Install and start a local MariaDB server for testing purposes


srcdir=$1
maxscaledir=$PWD/maxscale_test/
testdir=$PWD/local_test/

mkdir -p $testdir && cd $testdir

# Currently all tests that use npm are for the REST API
cp -t $testdir -r $srcdir/maxctrl/test/*
npm install

mkdir -p $maxscaledir && cd $maxscaledir

# Configure and install MaxScale
cmake $srcdir -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_INSTALL_PREFIX=$maxscaledir \
      -DBUILD_TESTS=Y \
      -DMAXSCALE_VARDIR=$maxscaledir \
      -DCMAKE_BUILD_TYPE=Debug \
      -DWITH_SCRIPTS=N \
      -DWITH_MAXSCALE_CNF=N \
      -DBUILD_CDC=Y \
      -DTARGET_COMPONENT=all \
      -DDEFAULT_MODULE_CONFIGDIR=$maxscaledir \
      -DDEFAULT_ADMIN_USER=`whoami`

make install

# Create required directories (we could run the postinst script but it's a bit too invasive)
mkdir -p $maxscaledir/lib64/maxscale
mkdir -p $maxscaledir/bin
mkdir -p $maxscaledir/share/maxscale
mkdir -p $maxscaledir/share/doc/MaxScale/maxscale
mkdir -p $maxscaledir/log/maxscale
mkdir -p $maxscaledir/lib/maxscale
mkdir -p $maxscaledir/cache/maxscale
mkdir -p $maxscaledir/run/maxscale
chmod 0755 $maxscaledir/log/maxscale
chmod 0755 $maxscaledir/lib/maxscale
chmod 0755 $maxscaledir/cache/maxscale
chmod 0755 $maxscaledir/run/maxscale

# This variable is used to start and stop MaxScale before each test
export MAXSCALE_DIR=$maxscaledir

# Run tests
cd $testdir
npm test
rval=$?

exit $rval
