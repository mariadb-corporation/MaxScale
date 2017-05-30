#!/bin/bash

#set -x

rp=`realpath $0`
export test_dir=`dirname $rp`
export test_name=`basename $rp`
$test_dir/non_native_setup $test_name
export ssl_options="--ssl-cert=$test_dir/ssl-cert/client-cert.pem --ssl-key=$test_dir/ssl-cert/client-key.pem"
#$test_dir/configure_maxscale.sh 
if [ $? -ne 0 ] ; then 
	echo "configure_maxscale.sh failed"
	exit 1
fi
#sleep 15

export Master_id=`echo "SELECT (@@server_id)" | mysql -u$node_user -p$node_password -h $node_000_network $ssl_options| tail -n1`
cat ./maxscale-mysqltest/fail.txt | grep "FAILED"

echo "Maister_id $Master_id"
$test_dir/Hartmut_tests/mariadb_tests_hartmut_imp 4006

cat $test_dir/Hartmut_tests/maxscale-mysqltest/fail.txt | grep "FAILED"

if [ $? -ne 0 ]; then
	cat $test_dir/Hartmut_tests/maxscale-mysqltest/fail.txt | grep "PASSED"
	if [ $? -ne 0 ]; then
		res=1
	else 
		res=0
	fi
else
	res=1
fi

$test_dir/copy_logs.sh mariadb_tests_hartmut
exit $res
