#!/bin/bash

#
# This script is run after each test block. It kills the MaxScale process.
#

maxscaledir=$MAXSCALE_DIR

test -z "$MAXSCALE_DIR" && exit 1

pkill '^maxscale$'

for ((i=0;i<100;i++))
do
    pgrep '^maxscale$' &> /dev/null || break
    sleep 0.1
done

# If it wasn't dead before, now it is
pgrep '^maxscale$' &> /dev/null && pkill -11 '^maxscale$'

exit 0
