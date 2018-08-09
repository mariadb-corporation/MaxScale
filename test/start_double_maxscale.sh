#!/bin/bash

#
# This script is run before each test block. It starts two MaxScales and waits
# for them to become responsive.
#

maxscaledir=$MAXSCALE_DIR

test -z "$MAXSCALE_DIR" && exit 1

# Create directories for both MaxScales

rm -r $maxscaledir/lib/maxscale
rm -r $maxscaledir/cache/maxscale
rm -r $maxscaledir/run/maxscale
rm -r $maxscaledir/secondary/lib/maxscale
rm -r $maxscaledir/secondary/cache/maxscale
rm -r $maxscaledir/secondary/run/maxscale
test -f /tmp/maxadmin.sock && rm /tmp/maxadmin.sock
test -f /tmp/maxadmin2.sock && rm /tmp/maxadmin2.sock

mkdir -m 0755 -p $maxscaledir/lib/maxscale/maxscale.cnf.d
mkdir -m 0755 -p $maxscaledir/cache/maxscale
mkdir -m 0755 -p $maxscaledir/run/maxscale
mkdir -m 0755 -p $maxscaledir/log/maxscale
mkdir -m 0755 -p $maxscaledir/secondary/lib/maxscale/maxscale.cnf.d
mkdir -m 0755 -p $maxscaledir/secondary/cache/maxscale
mkdir -m 0755 -p $maxscaledir/secondary/run/maxscale
mkdir -m 0755 -p $maxscaledir/secondary/log/maxscale

# Start MaxScale
$maxscaledir/bin/maxscale -df $maxscaledir/maxscale.cnf &>> $maxscaledir/maxscale1.output &

# Wait for the first MaxScale to start
for ((i=0;i<60;i++))
do
    $maxscaledir/bin/maxadmin help >& /dev/null && break
    sleep 0.1
done

# Start a second maxscale
$maxscaledir/bin/maxscale -df $maxscaledir/maxscale_secondary.cnf &>> $maxscaledir/maxscale2.output &

# Wait for the second MaxScale to start
for ((i=0;i<60;i++))
do
    $maxscaledir/bin/maxadmin -S /tmp/maxadmin2.sock help >& /dev/null && break
    sleep 0.1
done

# Give MaxScale some time to settle
sleep 1
