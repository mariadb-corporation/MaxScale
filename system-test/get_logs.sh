#!/bin/bash
#
# Copyright (c) 2016 MariaDB Corporation Ab
# Copyright (c) 2023 MariaDB plc, Finnish Branch
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2027-11-30
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

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
