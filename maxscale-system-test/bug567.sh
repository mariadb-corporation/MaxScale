#!/bin/bash

###
## @file bug567.sh Regression case for the bug "Crash if files from /dev/shm/ removed"
## - try to remove everythign from /dev/shm/$maxscale_pid
## check if Maxscale is alive

rp=`realpath $0`
export src_dir=`dirname $rp`
export test_dir=`pwd`
export test_name=`basename $rp`
$test_dir/non_native_setup $test_name

if [ $? -ne 0 ] ; then
        echo "configuring maxscale failed"
        exit 1
fi
export ssl_options="--ssl-cert=$src_dir/ssl-cert/client-cert.pem --ssl-key=$src_dir/ssl-cert/client-key.pem"

#pid=`ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $maxscale_access_user@$maxscale_IP "pgrep maxscale"`
#echo "Maxscale pid is $pid"
echo "removing log directory from /dev/shm/"
if [ $maxscale_IP != "127.0.0.1" ] ; then
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $maxscale_access_user@$maxscale_IP "sudo rm -rf /dev/shm/maxscale/*"
else
	sudo rm -rf /dev/shm/maxscale/*
fi
sleep 1
echo "checking if Maxscale is alive"
echo "show databases;" | mysql -u$node_user -p$node_password -h $maxscale_IP -P 4006 $ssl_options
res=$?

$src_dir/copy_logs.sh bug567
exit $res

