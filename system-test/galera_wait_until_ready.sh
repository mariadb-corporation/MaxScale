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

# Wait until the node is ready

for ((i=0;i<100;i++))
do
    mysql -ss $1 -e 'show status like "wsrep_ready"' | grep 'ON' && break || sleep 1
done
