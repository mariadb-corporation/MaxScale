#!/bin/bash

# This script builds and installs MaxScale, starts a MariaDB cluster and runs any
# tests that define a `npm test` target
#
# This is definitely not the most efficient way to test the binaries but it's a
# guaranteed method of creating a consistent and "safe" testing environment.

if [ $# -lt 3 ]
then
    echo "USAGE: $0 <MaxScale sources> <test sources> <test directory>"
    exit 1
fi

srcdir=$1
testsrc=$2
testdir=$3

maxscaledir=$PWD/maxscale_test/

rm -f $maxscaledir/maxscale{,1,2}.output $maxscaledir/{,secondary/}log/maxscale/maxscale.log

# Create the test directories
mkdir -p $maxscaledir $testdir

# Copy the common test files (start/stop scripts etc.)
cp -t $testdir -r $srcdir/test/*

# Copy test sources to test workspace
cp -t $testdir -r $testsrc/*

# Required by MaxCtrl (not super pretty)
cp -t $testdir/.. $srcdir/VERSION*.cmake

# This avoids running npm as root if we're executing the tests as root (MaxCtrl specific)
(cd $testdir && test -f configure_version.cmake && cmake -P configure_version.cmake)

# Copy required docker-compose files to the MaxScale directory and bring MariaDB
# servers up. This is an asynchronous process.
cd $maxscaledir
cp -t $maxscaledir -r $srcdir/test/*
docker-compose up -d || exit 1

# Install dependencies
cd $testdir
npm install || exit 1

# Configure and install MaxScale
cd $maxscaledir
cmake $srcdir -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_INSTALL_PREFIX=$maxscaledir \
      -DBUILD_TESTS=N \
      -DMAXSCALE_VARDIR=$maxscaledir \
      -DWITH_SCRIPTS=N \
      -DWITH_MAXSCALE_CNF=N \
      -DBUILD_CDC=N || exit 1

make -j $(grep -c processor /proc/cpuinfo) install || exit 1

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

# Run tests
npm test
rval=$?

# Stop MariaDB servers
if [ -z  "$SKIP_SHUTDOWN" ]
then
    cd $maxscaledir
    docker-compose down -v
fi

exit $rval
