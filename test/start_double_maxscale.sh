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

if [ "`whoami`" == "root" ]
then
    user_opt="-U root"
fi

# Start MaxScale
$maxscaledir/bin/maxscale $user_opt -f $maxscaledir/maxscale.cnf &>> $maxscaledir/maxscale1.output || exit 1

# Start a second maxscale
$maxscaledir/bin/maxscale $user_opt -f $maxscaledir/maxscale_secondary.cnf &>> $maxscaledir/maxscale2.output || exit 1

# Wait for the MaxScales to start

for ((i=0;i<150;i++))
do
    $maxscaledir/bin/maxctrl list servers >& /dev/null && \
    $maxscaledir/bin/maxctrl --hosts 127.0.0.1:8990 list servers >& /dev/null && \
        exit 0
    sleep 0.1
done

# MaxScales failed to start, exit with an error
exit 1
