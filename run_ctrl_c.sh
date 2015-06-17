#!/bin/bash

export test_name=run_ctrl_c
$test_dir/configure_maxscale.sh

scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -r $test_dir/test_ctrl_c/* $access_user@$maxscale_IP:./
ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $access_user@$maxscale_IP "export maxdir=$maxdir; $access_sudo ./test_ctrl_c.sh"

res=$?
$test_dir/copy_logs.sh run_ctrl_c
exit $res
