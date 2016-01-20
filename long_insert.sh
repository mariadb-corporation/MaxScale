#!/bin/bash

rp=`realpath $0`
export test_dir=`dirname $rp`
export test_name=`basename $rp`
$test_dir/configure_maxscale.sh
sleep 15

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
