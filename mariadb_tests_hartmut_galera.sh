#!/bin/bash

rp=`realpath $0`
export test_dir=`dirname $rp`
export test_name=`basename $rp`
$test_dir/configure_maxscale.sh 
sleep 15

export Master_id=`echo "SELECT (@@server_id)" | mysql -u$galera_user -p$galera_password -h $galera_000 | tail -n1`

echo "GRANT ALL PRIVILEGES ON *.* TO maxuser@'%' IDENTIFIED BY 'maxpwd' WITH GRANT OPTION; FLUSH PRIVILEGES;"  | mysql -u$galera_user -p$galera_password -h $galera_000
echo "GRANT ALL PRIVILEGES ON *.* TO maxuser@'localhost' IDENTIFIED BY 'maxpwd' WITH GRANT OPTION; FLUSH PRIVILEGES;"  | mysql -u$galera_user -p$galera_password -h $galera_000


$test_dir/Hartmut_tests/mariadb_tests_hartmut_imp 4006

ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $maxscale_access_user@$maxscale_IP "cat $maxscale_access_homedir/maxscale-mysqltest/fail.txt" | grep "FAILED"
if [ $? -ne 0 ]; then
	res=0
else
	res=1
fi

$test_dir/copy_logs.sh mariadb_tests_hartmut_galera
exit $res
