#!/bin/bash

scp -i $Maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$Maxscale_IP:$maxdir/log/* .
