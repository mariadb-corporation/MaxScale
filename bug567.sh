#!/bin/bash

export test_name=bug567
$test_dir/configure_maxscale.sh

pid=`ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "cat $maxdir/log/maxscale.pid"`
echo "Maxscale pid is $pid"
echo "removing log directory from /dev/shm/"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "rm -rf /dev/shm/$pid"
sleep 1
echo "checking if Maxscale is alive"
echo "show databases;" | mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4006 


res=$?

$test_dir/copy_logs.sh bug567
exit $res

