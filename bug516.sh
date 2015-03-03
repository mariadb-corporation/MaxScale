#!/bin/bash

export test_name=bug516
$test_dir/configure_maxscale.sh

echo "executing simple maxadmin command 100 times"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "echo 'for i in {1..100}; do $maxdir/bin/maxadmin -pskysql -uadmin -P6603 list clients; done' > $maxdir/bin/maxadmin_test.sh"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "chmod a+x $maxdir/bin/maxadmin_test.sh ; $maxdir/bin/maxadmin_test.sh "
echo "sleeping 100 seconds"
sleep 100
lines=`ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "netstat | grep 6603" | wc -l`
echo "Connections to the port 6603 $lines"
if [ "$lines" -gt 10 ]; then
	echo "there are more 10 connections to the port 6603"
	echo " "
	echo "netstat output"
	echo " "
	echo " "
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "netstat | grep 6603"
	res=1
else
	res=0
fi

$test_dir/copy_logs.sh bug516
exit $res
