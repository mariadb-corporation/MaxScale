#!/bin/bash

logs_dir="LOGS/$1"

rm -rf $logs_dir
mkdir -p $logs_dir

echo "log_dir:         $logs_dir"
echo "Maxscale_sshkey: $Maxscale_sshkey"
echo "Maxscale_IP:     $Maxscale_IP"
scp -i $Maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$Maxscale_IP:$maxdir/log/* $logs_dir
scp -i $Maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$Maxscale_IP:/tmp/core* $logs_dir
scp -i $Maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$Maxscale_IP:$maxdir/etc/* $logs_dir
chmod a+r $logs_dir/*

