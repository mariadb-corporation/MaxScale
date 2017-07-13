#!/bin/bash

#
# This script is run after each test block. It kills the MaxScale process
# and cleans up the directories that contain generated files.
#

maxscaledir=$MAXSCALE_DIR

test -z "$MAXSCALE_DIR" && exit 1

for ((i=0;i<20;i++))
do
    pkill '^maxscale$' || break
    sleep 0.5
done

# If it wasn't dead before, now it is
pkill -9 '^maxscale$'

rm -r $maxscaledir/lib/maxscale
rm -r $maxscaledir/cache/maxscale
rm -r $maxscaledir/run/maxscale
rm /tmp/maxadmin.sock

mkdir -m 0755 -p $maxscaledir/lib/maxscale
mkdir -m 0755 -p $maxscaledir/cache/maxscale
mkdir -m 0755 -p $maxscaledir/run/maxscale
