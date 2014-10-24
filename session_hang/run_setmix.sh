#!/bin/bash
for ((i=0 ; i<100 ; i++)) ; 
do
	mysql --host=$Maxscale_IP -P 4006 -u maxuser -pmaxpwd --verbose --force --unbuffered=true --disable-reconnect < /usr/local/skysql/maxscale/system-test/session_hang/setmix.sql
done

