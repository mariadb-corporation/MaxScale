#!/bin/bash

#
# This script is run before each test block. It starts MaxScale and waits for it
# to become responsive.
#

maxscaledir=$MAXSCALE_DIR

test -z "$MAXSCALE_DIR" && exit 1

# Start MaxScale
$maxscaledir/bin/maxscale -df $maxscaledir/maxscale.cnf &>> $maxscaledir/maxscale.output &
pid=$!

# Wait for MaxScale to start
for ((i=0;i<60;i++))
do
    $maxscaledir/bin/maxadmin help >& /dev/null && break
    sleep 0.1
done

# Give MaxScale some time to settle
sleep 1
