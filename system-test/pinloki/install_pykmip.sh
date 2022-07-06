#!/bin/bash

set -e

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
certificate_path=$HOME/certs/server-cert.pem
key_path=$HOME/certs/server-key.pem
ca_path=$HOME/certs/ca.pem
auth_suite=TLS1.2
policy_path=/tmp/policy
logging_level=DEBUG
database_path=/tmp/pykmip.db
# This disables the extended key usage checks, the ones the tests use don't have it set.
enable_tls_client_auth=False

[client]
hostname=127.0.0.1
port=5696
certfile=$HOME/certs/client-cert.pem
keyfile=$HOME/certs/client-key.pem
ca_certs=$HOME/certs/ca.pem
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
sudo systemctl daemon-reload
