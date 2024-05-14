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


# This is required by one of the tests
#
# TODO: Don't test correctness of routing with mysqltest
#

# TODO: Don't copy this and "unmangle" the test instead
cp -r $src_dir/Hartmut_tests/maxscale-mysqltest ./Hartmut_tests/maxscale-mysqltest/

master_id=`echo "SELECT @@server_id" | mysql -u$node_user -p$node_password -h $node_000_network $ssl_options -P $node_000_port | tail -n1`
echo "--disable_query_log" > Hartmut_tests/maxscale-mysqltest/testconf.inc
echo "SET @TMASTER_ID=$master_id;" >> Hartmut_tests/maxscale-mysqltest/testconf.inc
echo "--enable_query_log" >> Hartmut_tests/maxscale-mysqltest/testconf.inc

echo "--disable_query_log" > testconf.inc
echo "SET @TMASTER_ID=$master_id;" >> testconf.inc
echo "--enable_query_log" >> testconf.inc

$src_dir/mysqltest_driver.sh "$1" "$PWD/Hartmut_tests/maxscale-mysqltest" 4006

