#!/bin/bash

#set -x

if [ -z $1 ]; then
	echo "Test name missing"
	logs_dir="LOGS/nomane"
else

	logs_dir="LOGS/$1"
	rm -rf $logs_dir
fi
mkdir -p $logs_dir
scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$maxscale_IP:$maxdir/log/* $logs_dir
if [ $? -ne 0 ]; then
        echo "Error creating log dir"
fi


echo "log_dir:         $logs_dir"
echo "maxscale_sshkey: $maxscale_sshkey"
echo "maxscale_IP:     $maxscale_IP"
scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$maxscale_IP:$maxdir/log/* $logs_dir
if [ $? -ne 0 ]; then
	echo "Error copying Maxscale logs"
fi
scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$maxscale_IP:/tmp/core* $logs_dir
#scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$maxscale_IP:/root/core* $logs_dir
scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$maxscale_IP:$maxdir/etc/*.cnf $logs_dir
#ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$maxscale_IP "service maxscale stop"
#chmod a+r $logs_dir/*

