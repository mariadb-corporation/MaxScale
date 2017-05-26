#!/bin/bash
#set -x

export maxscale_sshkey=$maxscale_keyfile
if [ $maxscale_IP != "127.0.0.1" ] ; then
    ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet $maxscale_access_user@$maxscale_IP "mkdir -p logs; $maxscale_access_sudo cp $maxscale_log_dir/* logs/; $maxscale_access_sudo chmod a+r logs/*"
    scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet $maxscale_access_user@$maxscale_IP:logs/* .
    scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet $maxscale_access_user@$maxscale_IP:$maxscale_cnf .
else
    mkdir -p logs;
    sudo cp $maxscale_log_dir/* logs/
    cp $maxscale_cnf logs/
    sudo chmod a+r logs/*
    cp logs/* .
fi
