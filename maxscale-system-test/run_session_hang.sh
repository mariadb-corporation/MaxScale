#!/bin/bash

###
## @file run_session_hang.sh
## run a set of queries in the loop (see setmix.sql) using Perl client


export ssl_options="--ssl-cert=$src_dir/ssl-cert/client-cert.pem --ssl-key=$src_dir/ssl-cert/client-key.pem"

echo "drop table if exists t1; create table t1(id integer primary key); " | mysql -u$node_user -p$node_password -h${maxscale_000_network} -P 4006 $ssl_options test

if [ $? -ne 0 ]
then
    echo "Failed to create table test.t1"
    exit 1
fi

res=0

$src_dir/session_hang/run_setmix.sh &
perl $src_dir/session_hang/simpletest.pl
if [ $? -ne 0 ]
then
	res=1
fi

sleep 15

echo "show databases;" |  mysql -u$node_user -p$node_password -h${maxscale_000_network} -P 4006 $ssl_options
if [ $? -ne 0 ]
then
    res=1
fi

echo "Waiting for jobs"
wait

exit $res
