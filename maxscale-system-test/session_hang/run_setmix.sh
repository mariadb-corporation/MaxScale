#!/bin/bash
for ((i=0 ; i<100 ; i++)) ;
do
	echo "Iteration $i"
	mysql --host=${maxscale_000_network} -P 4006 -u $node_user -p$node_password --verbose --force --unbuffered=true --disable-reconnect $ssl_options > /dev/null < $src_dir/session_hang/setmix.sql >& /dev/null
done

