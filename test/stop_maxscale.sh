#!/bin/bash

#
# This script is run after each test block. It kills the MaxScale process.
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

exit 0
