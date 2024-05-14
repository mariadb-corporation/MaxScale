#!/bin/bash
#
# Copyright (c) 2022 MariaDB Corporation Ab
# Copyright (c) 2023 MariaDB plc, Finnish Branch
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2027-04-10
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

set -e

# We need python3
if ! command -v python3
then
    . /etc/os-release

    function install_yum {
        sudo yum install -y python3
    }

    function install_apt {
        sudo apt-get update && sudo apt-get -y install python3
    }

    case $ID in
        rhel)
            install_yum
            ;;
        centos)
            install_yum
            ;;
        rocky)
            install_yum
            ;;
        debian)
            install_apt
            ;;
        ubuntu)
            install_apt
            ;;
        *)
            echo "Don't know how to install Python3 for $ID"
            exit 1
            ;;
    esac
fi

# Set up a virtual python environments: this allows packages to be installed
# without a need for root access.
python3 -m venv /tmp/venv
. /tmp/venv/bin/activate
pip install --upgrade pip
pip install pykmip

cat <<EOF > /tmp/pykmip.conf
[server]
hostname=127.0.0.1
port=5696
certificate_path=$HOME/certs/mxs.crt
key_path=$HOME/certs/mxs.key
ca_path=$HOME/certs/ca.crt
auth_suite=TLS1.2
policy_path=/tmp/policy
logging_level=DEBUG
database_path=/tmp/pykmip.db
# This disables the extended key usage checks, the ones the tests use don't have it set.
enable_tls_client_auth=False

[client]
hostname=127.0.0.1
port=5696
certfile=$HOME/certs/mxs.crt
keyfile=$HOME/certs/mxs.key
ca_certs=$HOME/certs/ca.crt
EOF

# The policy directory can be empty but it must exist
mkdir -p /tmp/policy

# Create a SystemD unit to make it simpler to start and stop the server.
cat <<EOF > pykmip.service
[Unit]
Description="PyKMIP server"

[Service]
ExecStart=/tmp/venv/bin/pykmip-server -f /tmp/pykmip.conf --log_path=/tmp/pykmip.log

[Install]
WantedBy=default.target
EOF

sudo mv pykmip.service /etc/systemd/system/
sudo chcon system_u:object_r:systemd_unit_file_t:s0 /etc/systemd/system/pykmip.service
sudo chcon -R -t bin_t /tmp/venv/
sudo systemctl daemon-reload
