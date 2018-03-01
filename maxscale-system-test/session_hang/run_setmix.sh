#!/bin/bash
for ((i=0 ; i<100 ; i++)) ;
do
	mysql --host=$maxscale_IP -P 4006 -u $node_user -p$node_password --verbose --force --unbuffered=true --disable-reconnect $ssl_options > /dev/null < $test_dir/session_hang/setmix.sql >& /dev/null
done

