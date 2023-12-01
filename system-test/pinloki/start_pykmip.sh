#!/bin/bash
#
# Copyright (c) 2022 MariaDB Corporation Ab
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
