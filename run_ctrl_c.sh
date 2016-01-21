#!/bin/bash
rp=`realpath $0`
export test_dir=`dirname $rp`
export test_name=`basename $rp`

$test_dir/configure_maxscale.sh

scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -r $test_dir/test_ctrl_c/* $maxscale_access_user@$maxscale_IP:./
ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $maxscale_access_user@$maxscale_IP "export maxdir=$maxdir; export maxscale_access_sudo=$maxscale_access_sudo; ./test_ctrl_c.sh"

res=$?
$test_dir/copy_logs.sh run_ctrl_c
exit $res
