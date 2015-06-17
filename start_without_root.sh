#!/bin/bash

export test_name=start_without_root
$test_dir/configure_maxscale.sh

errmsg="MaxScale doesn't have write permission to MAXSCALE_HOME. Exiting"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo service maxscale stop" &
sleep 5

ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "cd $maxdir_bindir; ./maxscale -d -c ." 2>&1 | grep "$errmsg"
res=$?
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "cd $maxdir_bindir; ./maxscale -d -c ." 2>&1 | grep "$errmsg"
res1=$?

if [[ $res != 0  || $res1 != 0 ]] ; then
	echo "FAILED: no proper error message"
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "cd $maxdir_bindir; ./maxscale -d -c ." 
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "cd $maxdir_bindir; ./maxscale -c ." 
	$test_dir/copy_logs.sh start_without_root
	exit 1
fi
$test_dir/copy_logs.sh start_without_root
exit 0


