#!/bin/bash

###
## @file bug564.sh Regression case for the bug "Wrong charset settings"
## - call MariaDB client with different --default-character-set= settings
## - check output of SHOW VARIABLES LIKE 'char%'

rp=`realpath $0`
export test_dir=`pwd`
export test_name=`basename $rp`
$test_dir/non_native_setup $test_name

if [ $? -ne 0 ] ; then
        echo "configuring maxscale failed"
        exit 1
fi
export ssl_options="--ssl-cert=$test_dir/ssl-cert/client-cert.pem --ssl-key=$test_dir/ssl-cert/client-key.pem"

for char_set in "latin1" "latin2"
do

	line1=`mysql -u$node_user -p$node_password -h $maxscale_IP -P 4006 $ssl_options --default-character-set="$char_set" -e "SHOW VARIABLES LIKE 'char%'" | grep "character_set_client"`
	line2=`mysql -u$node_user -p$node_password -h $maxscale_IP -P 4006 $ssl_options --default-character-set="$char_set" -e "SHOW VARIABLES LIKE 'char%'" | grep "character_set_connection"`
	line3=`mysql -u$node_user -p$node_password -h $maxscale_IP -P 4006 $ssl_options --default-character-set="$char_set" -e "SHOW VARIABLES LIKE 'char%'" | grep "character_set_results"`

	echo $line1 | grep "$char_set"
	res1=$?
	echo $line2 | grep "$char_set"
	res2=$?
	echo $line3 | grep "$char_set"
	res3=$?


	if [[ $res1 != 0 ]] || [[ $res2 != 0 ]] || [[ $res3 != 0 ]] ; then
		echo "charset is ignored"
		mysql -u$node_user -p$node_password -h $maxscale_IP -P 4006 $ssl_options --default-character-set="latin2" -e "SHOW VARIABLES LIKE 'char%'"
		$test_dir/copy_logs.sh bug564
		exit 1
	fi
done
$test_dir/copy_logs.sh bug564
exit 0


