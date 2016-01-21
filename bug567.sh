#!/bin/bash

rp=`realpath $0`
export test_dir=`dirname $rp`
export test_name=`basename $rp`
$test_dir/configure_maxscale.sh
if [ $? -ne 0 ] ; then 
        echo "configure_maxscale.sh failed"
        exit 1
fi

sleep 15

pid=`ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $maxscale_access_user@$maxscale_IP "cat $maxdir/log/maxscale.pid"`
echo "Maxscale pid is $pid"
echo "removing log directory from /dev/shm/"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $maxscale_access_user@$maxscale_IP "rm -rf /dev/shm/$pid"
sleep 1
echo "checking if Maxscale is alive"
echo "show databases;" | mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4006 
res=$?

$test_dir/copy_logs.sh bug567
exit $res

