#!/bin/bash

export test_name=run_ctrl_c
$test_dir/configure_maxscale.sh

scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -r /usr/local/skysql/maxscale/system-test/test_ctrl_c/* root@$maxscale_IP:/home/ec2-user/
ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$maxscale_IP '/home/ec2-user/test_ctrl_c.sh'

res=$?
$test_dir/copy_logs.sh run_ctrl_c
exit $res
