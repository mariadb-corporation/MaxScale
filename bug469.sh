#!/bin/bash

## @file bug469 bug469 regression test case ("rwsplit counts every connection twice in master - counnection counts leak")
## - use maxadmin command "show server server1" and check "Current no. of conns" and "Number of connections" - both should be 0
## - execute simple query against RWSplit 
## - use maxadmin command "show server server1" and check "Current no. of conns" (should be 0) and "Number of connections" (should be 1)

export test_name=bug469

$test_dir/configure_maxscale.sh

res=0

ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "$maxdir/bin/maxadmin -p$maxadmin_password -uadmin -P6603 show server server1" 

ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "$maxdir/bin/maxadmin -p$maxadmin_password -uadmin -P6603 show server server1" | grep -P "Current no. of conns:\t\t0"
if [ $? != 0 ] ; then
	echo "Current no. of conns before query is not 0"
        res=1 
fi
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "$maxdir/bin/maxadmin -p$maxadmin_password -uadmin -P6603 show server server1" | grep -P "Number of connections:\t\t0"
if [ $? != 0 ] ; then
	echo "Number of connections before query is not 0"
        res=1 
fi



echo "Executing quiry"

ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "echo \"show databases;\" | mysql -h 127.0.0.1 -P 4006 -u$repl_user -p$repl_password -c"

sleep 10

ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "$maxdir/bin/maxadmin -p$maxadmin_password -uadmin -P6603 show server server1"

ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "$maxdir/bin/maxadmin -p$maxadmin_password -uadmin -P6603 show server server1" | grep -P "Current no. of conns:\t\t0"
if [ $? != 0 ] ; then
	echo "Current no. of conns after query is not 0"
        res=1 
fi
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "$maxdir/bin/maxadmin -p$maxadmin_password -uadmin -P6603 show server server1" | grep -P "Number of connections:\t\t1"
if [ $? != 0 ] ; then
	echo "Number of connections before query is not 1"
        res=1 
fi



$test_dir/copy_logs.sh bug469
exit $res
