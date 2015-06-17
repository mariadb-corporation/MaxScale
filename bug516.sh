#!/bin/bash

export test_name=bug516
$test_dir/configure_maxscale.sh

echo "executing simple maxadmin command 100 times"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo echo 'for i in {1..100}; do $maxdir_bin/maxadmin -p$maxadmin_password -uadmin -P6603 list clients; done' > $maxdir_bin/maxadmin_test.sh"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo chmod a+x $maxdir_bin/maxadmin_test.sh ; $maxdir_bin/maxadmin_test.sh "
echo "sleeping 100 seconds"
sleep 100
lines=`ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo netstat | grep 6603" | wc -l`
echo "Connections to the port 6603 $lines"
if [ "$lines" -gt 10 ]; then
	echo "there are more 10 connections to the port 6603"
	echo " "
	echo "netstat output"
	echo " "
	echo " "
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo netstat | grep 6603"
	res=1
else
	res=0
fi

$test_dir/copy_logs.sh bug516
exit $res
