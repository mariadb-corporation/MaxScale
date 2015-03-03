#!/bin/bash
set -x
scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$maxscale_IP:$maxdir/log/* .
