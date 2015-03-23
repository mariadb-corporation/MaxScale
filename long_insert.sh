#!/bin/bash

export test_name=long_insert

$test_dir/configure_maxscale.sh

IP=$Maxscale_IP

mysql -h $IP -P 4006 -u $repl_User -p$repl_Password < $test_dir/long_insert_sql/test_init.sql

echo "RWSplit router:"
for ((i=0 ; i<1000 ; i++)) ; do 
	echo "iteration: $i"
	mysql -h $IP -P 4006 -u $repl_User -p$repl_Password < $test_dir/long_insert_sql/test_query.sql 
done

echo "ReadConn router (master):"
for ((i=0 ; i<1000 ; i++)) ; do 
        echo "iteration: $i"
        mysql -h $IP -P 4008 -u $repl_User -p$repl_Password < $test_dir/long_insert_sql/test_query.sql 
done


res=$?

$test_dir/copy_logs.sh long_insert
exit $res
