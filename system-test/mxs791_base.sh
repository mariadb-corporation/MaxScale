#!/bin/bash
#
# Copyright (c) 2016 MariaDB Corporation Ab
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

rp=`realpath $0`

export ssl_options="--ssl-cert=$src_dir/ssl-cert/client.crt --ssl-key=$src_dir/ssl-cert/client.key"

res=0
echo "Trying RWSplit"
echo "show tables" | mysql -u$maxscale_user -p$maxscale_password -h ${maxscale_000_network} -P 4006 $ssl_option test
if [ $? != 0 ] ; then
        res=1
        echo "Can't connect to DB 'test'"
fi

echo "Trying ReadConn master"
echo "show tables" | mysql -u$maxscale_user -p$maxscale_password -h ${maxscale_000_network} -P 4008 $ssl_options test
if [ $? != 0 ] ; then
        res=1
        echo "Can't connect to DB 'test'"
fi

echo "Trying ReadConn slave"
echo "show tables" | mysql -u$maxscale_user -p$maxscale_password -h ${maxscale_000_network} -P 4009 $ssl_options test
if [ $? != 0 ] ; then
        res=1
        echo "Can't connect to DB 'test'"
fi

exit $res
