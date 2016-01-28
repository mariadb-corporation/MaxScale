#!/bin/bash

#bug 454
rp=`realpath $0`
export test_dir=`dirname $rp`
export test_name=`basename $rp`

$test_dir/configure_maxscale.sh
if [ $? -ne 0 ] ; then 
        echo "configure_maxscale.sh failed"
        exit 1
fi


echo "drop table if exists t1; create table t1(id integer primary key); " | mysql -u$repl_user -p$repl_password -h$maxscale_IP -p 4006 test
echo "drop table if exists t1; create table t1(id integer primary key); " | mysql -u$repl_user -p$repl_password -h$maxscale_IP -p 4006 mysql

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


echo "show databases;" |  mysql -u$repl_user -p$repl_password -h$maxscale_IP -p 4006
if [ $? -ne 0 ] ; then 
        res=1
fi

$test_dir/copy_logs.sh run_session_hang

exit $res
