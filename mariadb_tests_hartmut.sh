#!/bin/bash

export test_name=mariadb_tests_hartmut

$test_dir/configure_maxscale.sh &
sleep 15

export Master_id=`echo "SELECT (@@server_id)" | mysql -u$repl_user -p$repl_password -h $repl_000 | tail -n1`
$test_dir/Hartmut_tests/mariadb_tests_hartmut_imp 4006

ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@$maxscale_IP "cat /home/ec2-user/maxscale-mysqltest/fail.txt" | grep "FAILED"
if [ $? -ne 0 ]; then
        res=0
else 
        res=1
fi

$test_dir/copy_logs.sh mariadb_tests_hartmut
exit $res
