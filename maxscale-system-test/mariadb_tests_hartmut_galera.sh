#!/bin/bash

rp=`realpath $0`
export test_dir=`dirname $rp`
export test_name=`basename $rp`
#$test_dir/configure_maxscale.sh 
$test_dir/non_native_setup $test_name
if [ $? -ne 0 ] ; then 
        echo "configure_maxscale.sh failed"
        exit 1
fi
export ssl_options="--ssl-cert=$test_dir/ssl-cert/client-cert.pem --ssl-key=$test_dir/ssl-cert/client-key.pem"

export Master_id=`echo "SELECT (@@server_id)" | mysql -u$galera_user -p$galera_password -h $galera_000_network  $ssl_options | tail -n1`

echo "GRANT ALL PRIVILEGES ON *.* TO maxuser@'%' IDENTIFIED BY 'maxpwd' WITH GRANT OPTION; FLUSH PRIVILEGES;"  | mysql -u$galera_user -p$galera_password -h $galera_000  $ssl_options
echo "GRANT ALL PRIVILEGES ON *.* TO maxuser@'localhost' IDENTIFIED BY 'maxpwd' WITH GRANT OPTION; FLUSH PRIVILEGES;"  | mysql -u$galera_user -p$galera_password -h $galera_000  $ssl_options

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

$test_dir/copy_logs.sh mariadb_tests_hartmut_galera
exit $res
