#!/bin/bash

maxdir="/usr/local/skysql/maxscale/"
cd $maxdir/bin
export MAXSCALE_HOME=$maxdir

service maxscale stop

/home/ec2-user/start_killer.sh &

T="$(date +%s)"

./maxscale -d

T="$(($(date +%s)-T))"
echo "Time in seconds: ${T} (including 5 seconds before kill)"

if [ "$T" -lt 10 ] ; then
        echo "PASSED"
        exit 0
else
        echo "FAILED"
        exit 1
fi

