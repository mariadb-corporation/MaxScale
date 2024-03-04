#!/bin/bash
#
# Copyright (c) 2016 MariaDB Corporation Ab
# Copyright (c) 2023 MariaDB plc, Finnish Branch
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2028-02-27
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

function install_package
{
    local keyfile=$1
    local user=$2
    local host=$3
    local package=$4

    echo "Installing $package on $host."

    ssh -i ${keyfile} -o StrictHostKeyChecking=no ${user}@${host} sudo yum -y install ${package}

    if [ $? -ne 0 ]
    then
        echo "error: Could not install $package on $host."
        exit 1
    fi
}

function start_service
{
    local keyfile=$1
    local user=$2
    local host=$3
    local service=$4

    echo "Starting $package on $host."

    ssh -i ${keyfile} -o StrictHostKeyChecking=no ${user}@${host} sudo systemctl start ${service}

    if [ $? -ne 0 ]
    then
        echo "error: Could not start $service on $host."
        exit 1
    fi
}

function install_redis_on_maxscale_000
{
    install_package ${maxscale_000_keyfile} ${maxscale_000_whoami} ${maxscale_000_network} redis
}

function start_redis_on_maxscale_000
{
    start_service ${maxscale_000_keyfile} ${maxscale_000_whoami} ${maxscale_000_network} redis
}

function install_memcached_on_maxscale_000
{
    install_package ${maxscale_000_keyfile} ${maxscale_000_whoami} ${maxscale_000_network} memcached
}

function start_memcached_on_maxscale_000
{
    start_service ${maxscale_000_keyfile} ${maxscale_000_whoami} ${maxscale_000_network} memcached
}

install_memcached_on_maxscale_000
start_memcached_on_maxscale_000

install_redis_on_maxscale_000
start_redis_on_maxscale_000
