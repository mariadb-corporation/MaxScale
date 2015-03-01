#!/bin/bash

set -x

if [ -z $1 ]; then
	echo "Test name missing"
	logs_dir="LOGS/nomane"
else

	logs_dir="LOGS/$1"
	rm -rf $logs_dir
fi
mkdir -p $logs_dir
scp -i $Maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$Maxscale_IP:$maxdir/log/* $logs_dir
if [ $? -ne 0 ]; then
        echo "Error creating log dir"
fi


echo "log_dir:         $logs_dir"
echo "Maxscale_sshkey: $Maxscale_sshkey"
echo "Maxscale_IP:     $Maxscale_IP"
scp -i $Maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$Maxscale_IP:$maxdir/log/* $logs_dir
if [ $? -ne 0 ]; then
	echo "Error copying Maxscale logs"
fi
scp -i $Maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$Maxscale_IP:/tmp/core* $logs_dir
#scp -i $Maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$Maxscale_IP:/root/core* $logs_dir
scp -i $Maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$Maxscale_IP:$maxdir/etc/*.cnf $logs_dir
#ssh -i $Maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$Maxscale_IP "service maxscale stop"
#chmod a+r $logs_dir/*

