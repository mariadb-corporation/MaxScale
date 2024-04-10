#
# Copyright (c) 2016 MariaDB Corporation Ab
# Copyright (c) 2023 MariaDB plc, Finnish Branch
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2028-04-03
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

set -x

dir=`pwd`


#cp ~/build-scripts/test/multiple_servers.cnf $dir
sudo killall mysqld
sudo killall mysql_install_db
sleep 10
rm -rf /data/mysql/mysql$1
rm -rf /var/log/mysql/*
mkdir -p /data/mysql/mysql$1
chown mysql:mysql -R /data
chown mysql:mysql -R /var/run/mysqld

mysql_install_db --defaults-file=$dir/local_tests/multiple_servers.cnf --user=mysql --datadir=/data/mysql/mysql$1
