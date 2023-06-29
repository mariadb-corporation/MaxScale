#!/bin/bash

#
# This script is run before each test block. It starts two MaxScales and waits
# for them to become responsive.
#

maxscaledir=$MAXSCALE_DIR

test -z "$MAXSCALE_DIR" && exit 1

# Create directories for both MaxScales

test -f $maxscaledir/data/run/maxscale.pid && kill -9 $(cat $maxscaledir/data/run/maxscale.pid)
test -f $maxscaledir/secondary/data/run/maxscale.pid && kill -9 $(cat $maxscaledir/secondary/data/run/maxscale.pid)

rm -r $maxscaledir/data/ $maxscaledir/secondary/data/

mkdir -m 0755 -p $maxscaledir/{,secondary}/data/{lib,cache,language,run,maxscale.cnf.d}/

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
    curl -s -f -u admin:mariadb 127.0.0.1:8989/v1/servers >& /dev/null && \
        curl -s -f -u admin:mariadb 127.0.0.1:8990/v1/servers >& /dev/null && \
        exit 0
    sleep 0.1
done

# MaxScales failed to start, exit with an error
exit 1
