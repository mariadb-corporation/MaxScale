#!/bin/bash

# This script builds and installs MaxScale, starts a MariaDB cluster and runs any
# tests that define a `npm test` target
#
# This is definitely not the most efficient way to test the binaries but it's a
# guaranteed method of creating a consistent and "safe" testing environment.
#
# Test run customization:
#
# NUMCPU:                  The number of parallel build jobs to use.
# SKIP_SHUTDOWN:           If set, leaves the docker-compose setup up.
# MXS_EXTRA_CMAKE_OPTIONS: Extra CMake options passed to the configuration step
#
if [ $# -lt 3 ]
then
    echo "USAGE: $0 <MaxScale sources> <test sources> <test directory>"
    exit 1
fi

# Prevent failures in case if Docker is not available
command -v docker
if [ $? != 0 ]
then
    echo "Docker is not available, skipping the test"
    exit 0
fi

command -v docker-compose
if [ $? != 0 ]
then
    echo "docker-compose is not available, skipping the test"
    exit 0
fi

if [ -z "$NUMCPU" ]
then
    export NUMCPU=$(grep -c processor /proc/cpuinfo)
fi

srcdir=$1
testsrc=$2
testdir=$3

maxscaledir=$PWD/maxscale_test/

rm -f $maxscaledir/maxscale1.output $maxscaledir/log/maxscale/maxscale.log

# Create the test directories
mkdir -p $maxscaledir $testdir

# Copy the common test files (start/stop scripts etc.)
cp -p -t $testdir -r $srcdir/test/*

# Copy test sources to test workspace
cp -p -t $testdir -r $testsrc/*

# Required by MaxCtrl (not super pretty)
cp -p -t $testdir/.. $srcdir/VERSION*.cmake

# This avoids running npm as root if we're executing the tests as root (MaxCtrl specific)
(cd $testdir && test -f configure_version.cmake && cmake -P configure_version.cmake)

# Copy required docker-compose files to the MaxScale directory and bring MariaDB
# servers up. This is an asynchronous process.
cd $maxscaledir
cp -p -t $maxscaledir -r $srcdir/test/*
docker-compose up -d || exit 1

# Install dependencies
cd $testdir
npm install || exit 1

# UBSAN won't abort the process without these options
export UBSAN_OPTIONS=abort_on_error=1:print_stacktrace=1

# Configure and install MaxScale
cd $maxscaledir
cmake $srcdir -DCMAKE_BUILD_TYPE=Debug \
      -DWITH_ASAN=Y \
      -DWITH_UBSAN=Y \
      -DCMAKE_INSTALL_PREFIX=$maxscaledir \
      -DDEFAULT_CONFIGSUBDIR=$maxscaledir \
      -DDEFAULT_SYSTEMD_CONFIGDIR=$maxscaledir \
      -DDEFAULT_MODULE_CONFIGDIR=$maxscaledir \
      -DMAXSCALE_VARDIR=$maxscaledir \
      -DWITH_SCRIPTS=N \
      -DWITH_MAXSCALE_CNF=N \
      $MXS_EXTRA_CMAKE_OPTIONS || exit 1

make -j $NUMCPU install || exit 1

# Create required directories (we could run the postinst script but it's a bit too invasive)
mkdir -p -m 0755 $maxscaledir/{lib,lib64,share,log,cache,run}/maxscale
mkdir -p -m 0755 $maxscaledir/bin
mkdir -p -m 0755 $maxscaledir/share/doc/MaxScale/maxscale
mkdir -p -m 0755 $maxscaledir/etc/{maxscale.cnf.d,maxscale.modules.d}

# This variable is used to start and stop MaxScale before each test
export MAXSCALE_DIR=$maxscaledir

# Wait until the servers are up
cd $maxscaledir
for node in server1 server2 server3 server4
do
    printf "Waiting for $node to start... "
    for ((i=0; i<60; i++))
    do
        docker exec -i $node mysql -umaxuser -pmaxpwd -e "select 1" >& /dev/null && break
        sleep 1
    done

    docker exec -i $node mysql -umaxuser -pmaxpwd -e "select 1" >& /dev/null

    if [ $? -ne 0 ]
    then
        echo "failed to start $node, error is:"
        docker exec -i $node mysql -umaxuser -pmaxpwd -e "select 1"
        exit 1
    else
        echo "Done!"
    fi
done

# Go to the test directory
cd $testdir

# Make sure no stale processes of files are left from an earlier run
./stop_maxscale.sh
./start_maxscale.sh

# Run tests
npm test
rval=$?

./stop_maxscale.sh

# Stop MariaDB servers
if [ -z  "$SKIP_SHUTDOWN" ]
then
    cd $maxscaledir
    docker-compose down -v
fi

exit $rval
