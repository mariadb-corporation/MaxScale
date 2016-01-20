#!/bin/bash

rp=`realpath $0`
export test_dir=`dirname $rp`
export test_name=`basename $rp`
$test_dir/configure_maxscale.sh

echo "Waiting for 15 seconds"
sleep 15

for char_set in "latin1" "latin2"
do

	line1=`mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4006 --default-character-set="$char_set" -e "SHOW VARIABLES LIKE 'char%'" | grep "character_set_client"`
	line2=`mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4006 --default-character-set="$char_set" -e "SHOW VARIABLES LIKE 'char%'" | grep "character_set_connection"`
	line3=`mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4006 --default-character-set="$char_set" -e "SHOW VARIABLES LIKE 'char%'" | grep "character_set_results"`

	echo $line1 | grep "$char_set"
	res1=$?
	echo $line2 | grep "$char_set"
	res2=$?
	echo $line3 | grep "$char_set"
	res3=$?


	if [[ $res1 != 0 ]] || [[ $res2 != 0 ]] || [[ $res3 != 0 ]] ; then 
		echo "charset is ignored"
		mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4006 --default-character-set="latin2" -e "SHOW VARIABLES LIKE 'char%'" 
		$test_dir/copy_logs.sh bug564
		exit 1
	fi
done
$test_dir/copy_logs.sh bug564
exit 0


