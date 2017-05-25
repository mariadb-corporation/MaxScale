#!/bin/bash

#
# This script is run after each test block. It kills the MaxScale process
# and cleans up the directories that contain generated files.
#

test -z "$MAXSCALE_DIR" && exit 1

maxscaledir=$MAXSCALE_DIR

for ((i=0;i<10;i++))
do
    pkill maxscale || break
    sleep 0.5
done

# If it wasn't dead before, now it is
pgrep maxscale && pkill -9 maxscale

# Remove created users
rm $maxscaledir/passwd
rm $maxscaledir/maxadmin-users

rm -r $maxscaledir/lib/maxscale
rm -r $maxscaledir/cache/maxscale
rm -r $maxscaledir/run/maxscale
mkdir -m 0755 -p $maxscaledir/lib/maxscale
mkdir -m 0755 -p $maxscaledir/cache/maxscale
mkdir -m 0755 -p $maxscaledir/run/maxscale
