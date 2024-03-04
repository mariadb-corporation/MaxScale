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

rp=`realpath $0`
export src_dir=`dirname $rp`
export test_dir=`pwd`
export test_name=`basename $rp`
$test_dir/non_native_setup $test_name
export ssl_options="--ssl-cert=$src_dir/ssl-cert/client-cert.pem --ssl-key=$src_dir/ssl-cert/client-key.pem"

IP=$Maxscale_IP

mysql -h $IP -P 4006 -u $node_user -p$node_password $ssl_options < $src_dir/long_insert_sql/test_init.sql

echo "RWSplit router:"
for ((i=0 ; i<1000 ; i++)) ; do
	echo "iteration: $i"
	mysql -h $IP -P 4006 -u $node_user -p$node_password $ssl_options < $src_dir/long_insert_sql/test_query.sql
done

echo "ReadConn router (master):"
for ((i=0 ; i<1000 ; i++)) ; do
        echo "iteration: $i"
        mysql -h $IP -P 4008 -u $node_user -p$node_uassword $ssl_options < $src_dir/long_insert_sql/test_query.sql
done


res=$?

$src_dir/copy_logs.sh long_insert
exit $res
