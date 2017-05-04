#!/bin/bash

#
# This script is run after each test block. It kills the MaxScale process
# and cleans up the directories that contain generated files.
#

test -z "$MAXSCALE_DIR" && exit 1

maxscaledir=$MAXSCALE_DIR

pid=`cat $maxscaledir/maxscale.pid`
echo $pid

for ((i=0;i<60;i++))
do
    kill -0 $pid

    if [ $? -eq 0 ]
    then
        # Process is still up
        kill $pid

        if [ $i -gt 3 ]
        then
            sleep 0.1
        fi
    else
        break
    fi
done

rm -r $maxscaledir/lib/maxscale
rm -r $maxscaledir/cache/maxscale
rm -r $maxscaledir/run/maxscale
mkdir -m 0755 -p $maxscaledir/lib/maxscale
mkdir -m 0755 -p $maxscaledir/cache/maxscale
mkdir -m 0755 -p $maxscaledir/run/maxscale
