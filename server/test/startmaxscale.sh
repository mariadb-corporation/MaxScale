#!/bin/sh
killall -KILL maxscale
sleep 1
setsid $1/maxscale $2
exit 0
