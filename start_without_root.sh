#!/bin/bash

export test_name=start_without_root
$test_dir/configure_maxscale.sh

errmsg="MaxScale doesn't have write permission to MAXSCALE_HOME. Exiting"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "service maxscale stop" &
sleep 5

ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ec2-user@$maxscale_IP "cd $maxdir; bin/maxscale -d -c ." 2>&1 | grep "$errmsg"
res=$?
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ec2-user@$maxscale_IP "cd $maxdir; bin/maxscale -d -c ." 2>&1 | grep "$errmsg"
res1=$?

if [[ $res != 0  || $res1 != 0 ]] ; then
	echo "FAILED: no proper error message"
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ec2-user@$maxscale_IP "cd $maxdir; bin/maxscale -d -c ." 
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ec2-user@$maxscale_IP "cd $maxdir; bin/maxscale -c ." 
	$test_dir/copy_logs.sh start_without_root
	exit 1
fi
$test_dir/copy_logs.sh start_without_root
exit 0


