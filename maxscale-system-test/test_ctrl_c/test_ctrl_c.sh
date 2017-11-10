#!/bin/bash

$maxscale_access_sudo service maxscale stop

hm=`pwd`
$hm/start_killer.sh &
if [ $? -ne 0 ] ; then
        exit 1
fi

T="$(date +%s)"

$maxscale_access_sudo maxscale -d -U maxscale
if [ $? -ne 0 ] ; then
	exit 1
fi

T="$(($(date +%s)-T))"
echo "Time in seconds: ${T} (including 5 seconds before kill)"

if [ "$T" -lt 10 ] ; then
        echo "PASSED"
        exit 0
else
        echo "FAILED"
        exit 1
fi

