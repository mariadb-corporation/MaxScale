#!/bin/bash

set -e

sudo systemctl start pykmip.service

# Give it a few seconds to start up
sleep 2

# Create a new key
/tmp/venv/bin/python3 <<EOF
from kmip.pie.client import ProxyKmipClient, enums
from kmip.core.enums import CryptographicAlgorithm
c = ProxyKmipClient(config_file='/tmp/pykmip.conf')
c.open()
uid = c.create(CryptographicAlgorithm.AES, 256)

with open('.pykmip-key', 'w') as f:
    f.write(uid)
EOF

# The key ID in MaxScale is the UID of the key returned by the KMIP server.
KEY_UID=$(cat .pykmip-key)
sudo sed -i "s/encryption_key_id.*/encryption_key_id=$KEY_UID/" /etc/maxscale.cnf
