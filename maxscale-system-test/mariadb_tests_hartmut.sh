#!/bin/bash

# This is required by one of the tests
#
# TODO: Don't test correctness of routing with mysqltest
#
rp=`realpath $0`
export src_dir=`dirname $rp`

# TODO: Don't copy this and "unmangle" the test instead
cp -r $src_dir/Hartmut_tests/maxscale-mysqltest ./Hartmut_tests/maxscale-mysqltest/

master_id=`echo "SELECT @@server_id" | mysql -u$node_user -p$node_password -h $node_000_network $ssl_options -P $node_000_port | tail -n1`
echo "--disable_query_log" > Hartmut_tests/maxscale-mysqltest/testconf.inc
echo "SET @TMASTER_ID=$master_id;" >> Hartmut_tests/maxscale-mysqltest/testconf.inc
echo "--enable_query_log" >> Hartmut_tests/maxscale-mysqltest/testconf.inc

$src_dir/mysqltest_driver.sh $1 $PWD/Hartmut_tests/maxscale-mysqltest 4006

ret=$?
$src_dir/copy_logs.sh $1

exit $ret
