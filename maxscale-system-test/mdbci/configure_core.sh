#!/bin/bash
set -x

ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $maxscale_access_user@$maxscale_IP '$maxscale_access_sudo service iptables stop'

ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $maxscale_access_user@$maxscale_IP "$maxscale_access_sudo mkdir ccore; $maxscale_access_sudo chown $maxscale_access_user ccore"

scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no ${script_dir}/add_core_cnf.sh $maxscale_access_user@$maxscale_IP:./ccore/
ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $maxscale_access_user@$maxscale_IP "$maxscale_access_sudo /home/$maxscale_access_user/ccore/add_core_cnf.sh"

set +x
