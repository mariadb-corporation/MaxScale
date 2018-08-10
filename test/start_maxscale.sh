#!/bin/bash

#
# This script is run before each test block. It starts MaxScale and waits for it
# to become responsive.
#

maxscaledir=$MAXSCALE_DIR

test -z "$MAXSCALE_DIR" && exit 1

rm -r $maxscaledir/lib/maxscale
rm -r $maxscaledir/cache/maxscale
rm -r $maxscaledir/run/maxscale
test -f /tmp/maxadmin.sock && rm /tmp/maxadmin.sock

mkdir -m 0755 -p $maxscaledir/lib/maxscale/maxscale.cnf.d
mkdir -m 0755 -p $maxscaledir/cache/maxscale
mkdir -m 0755 -p $maxscaledir/run/maxscale
mkdir -m 0755 -p $maxscaledir/log/maxscale

if [ "`whoami`" == "root" ]
then
    user_opt="-U root"
fi

# Start MaxScale
$maxscaledir/bin/maxscale $user_opt -f $maxscaledir/maxscale.cnf &>> $maxscaledir/maxscale.output

# Wait for MaxScale to start
for ((i=0;i<150;i++))
do
    $maxscaledir/bin/maxctrl list servers >& /dev/null && break
    sleep 0.1
done
