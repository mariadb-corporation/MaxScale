#!/bin/bash
set -x
scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $access_user@$maxscale_IP:$maxscale_log_dir/* .
scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $access_user@$maxscale_IP:$maxscale_cnf .
