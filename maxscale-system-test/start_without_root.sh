#!/bin/bash

rp=`realpath $0`
export test_dir=`pwd`
export test_name=`basename $rp`

$test_dir/non_native_setup $test_name

errmsg="MaxScale doesn't have write permission to MAXSCALE_HOME. Exiting"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $maxscale_access_user@$maxscale_IP "$maxscale_access_sudo service maxscale stop" &
sleep 5

conf_dir=$(dirname "${maxscale_cnf}")
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $maxscale_access_user@$maxscale_IP "maxscale -d -c $conf_dir" 2>&1 | grep "$errmsg"
res=$?
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $maxscale_access_user@$maxscale_IP "maxscale -d -c $conf_dir" 2>&1 | grep "$errmsg"
res1=$?

if [[ $res != 0  || $res1 != 0 ]] ; then
	echo "FAILED: no proper error message"
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $maxscale_access_user@$maxscale_IP "maxscale -d -c $conf_dir"
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $maxscale_access_user@$maxscale_IP "maxscale -c $cond_dir"
	$test_dir/copy_logs.sh start_without_root
	exit 1
fi
$test_dir/copy_logs.sh start_without_root
exit 0
