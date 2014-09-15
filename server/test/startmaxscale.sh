#!/bin/sh
killall -KILL maxscale
sleep 1
nohup $1/maxscale $2 &
trap "echo trap triggered." SIGABRT
exit 0
