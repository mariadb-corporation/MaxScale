#!/bin/bash

###
## @file run_ctrl_c.sh
## check that Maxscale is reacting correctly on ctrc+c signal and termination does not take ages

rp=`realpath $0`
export test_dir=`dirname $rp`
export test_name=`basename $rp`

if [ $maxscale_IP == "127.0.0.1" ] ; then
	echo local test is not supporte
	exit 0
fi

$test_dir/non_native_setup $test_name

if [ $? -ne 0 ] ; then
        echo "configuring maxscale failed"
        exit 1
fi

scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -r $test_dir/test_ctrl_c/* $maxscale_access_user@$maxscale_IP:./
ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $maxscale_access_user@$maxscale_IP "export maxscale_access_sudo=$maxscale_access_sudo; ./test_ctrl_c.sh"

res=$?

ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $maxscale_access_user@$maxscale_IP "sudo rm -f /tmp/maxadmin.sock"

$test_dir/copy_logs.sh run_ctrl_c
exit $res
