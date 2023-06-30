#!/bin/bash

#
# This script is run before each test block. It starts MaxScale and waits for it
# to become responsive.
#

maxscaledir=$MAXSCALE_DIR

test -z "$MAXSCALE_DIR" && exit 1

test -f $maxscaledir/data/run/maxscale.pid && kill -9 $(cat $maxscaledir/data/run/maxscale.pid)

rm -r $maxscaledir/data/

mkdir -m 0755 -p $maxscaledir/data/{lib,cache,language,run,maxscale.cnf.d}/

if [ "`whoami`" == "root" ]
then
    user_opt="-U root"
fi

# Start MaxScale
$maxscaledir/bin/maxscale $user_opt -f $maxscaledir/maxscale.cnf &>> $maxscaledir/maxscale.output || exit 1

# Wait for MaxScale to start
for ((i=0;i<150;i++))
do
    curl -s -f -u admin:mariadb 127.0.0.1:8989/v1/servers >& /dev/null && exit 0
    sleep 0.1
done

# MaxScale failed to start, exit with an error
exit 1
