#
# Copyright (c) 2016 MariaDB Corporation Ab
# Copyright (c) 2023 MariaDB plc, Finnish Branch
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2028-01-30
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

set -x
cp  ${MDBCI_VM_PATH}/${mdbci_config_name}.json LOGS/
cp -r ${MDBCI_VM_PATH}/${mdbci_config_name} LOGS/
rm -rf LOGS/${mdbci_config_name}/*.pem
rsync -a --no-o --no-g LOGS ${logs_publish_dir}
chmod a+r ${logs_publish_dir}/*
