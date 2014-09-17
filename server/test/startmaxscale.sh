#!/bin/sh
killall -KILL maxscale
sleep 1
/bin/sh $1/maxscale $2 &
exit 0
