#!/bin/bash

#bug 454
rp=`realpath $0`
export test_dir=`dirname $rp`
export test_name=`basename $rp`

$test_dir/non_native_setup $test_name
#$test_dir/configure_maxscale.sh
if [ $? -ne 0 ] ; then 
        echo "configure_maxscale.sh failed"
        exit 1
fi
export ssl_options="--ssl-cert=$test_dir/ssl-cert/client-cert.pem --ssl-key=$test_dir/ssl-cert/client-key.pem"

set -x
echo "drop table if exists t1; create table t1(id integer primary key); " | mysql -u$node_user -p$node_password -h$maxscale_IP -P 4006 $ssl_options test

if [ $? -ne 0 ]
then
    echo "Failed to create table test.t1"
    exit 1
fi

echo "drop table if exists t1; create table t1(id integer primary key); " | mysql -u$node_user -p$node_password -h$maxscale_IP -P 4006 $ssl_options mysql

if [ $? -ne 0 ]
then
    echo "Failed to create table mysql.t1"
    exit 1
fi

set +x

$test_dir/session_hang/run_setmix.sh &
perl $test_dir/session_hang/simpletest.pl
if [ $? -ne 0 ] ; then 
	res=1
fi

sleep 15

$test_dir/session_hang/run_setmix.sh &
perl $test_dir/session_hang/simpletest.pl
if [ $? -ne 0 ] ; then 
        res=1
fi

sleep 15

$test_dir/session_hang/run_setmix.sh &
perl $test_dir/session_hang/simpletest.pl
if [ $? -ne 0 ] ; then 
        res=1
fi

sleep 15

$test_dir/session_hang/run_setmix.sh &
perl $test_dir/session_hang/simpletest.pl
if [ $? -ne 0 ] ; then 
        res=1
fi

sleep 15


echo "show databases;" |  mysql -u$node_user -p$node_password -h$maxscale_IP -P 4006 $ssl_options
if [ $? -ne 0 ] ; then 
        res=1
fi

$test_dir/copy_logs.sh run_session_hang

wait
if [ $res -eq 1 ]
then
    echo "Test FAILED"
else
    echo "Test PASSED"
fi

exit $res
