#!/bin/bash

###
## @file bug562.sh Regression case for the bug "Wrong error message for Access denied error"
## - try to connect with bad credentials directly to MariaDB server and via Maxscale
## - compare error messages

export ssl_options="--ssl-cert=$src_dir/ssl-cert/client-cert.pem --ssl-key=$src_dir/ssl-cert/client-key.pem"

mariadb_err=`mysql -u no_such_user -psome_pwd -h $node_001_network $ssl_option $node_001_socket_cmd test 2>&1`
maxscale_err=`mysql -u no_such_user -psome_pwd -h ${maxscale_000_network} -P 4006  $ssl_options test 2>&1`

echo "MariaDB message"
echo "$mariadb_err"
echo " "
echo "Maxscale message"
echo "$maxscale_err"

res=0
#echo "$maxscale_err" | grep "$mariadb_err"
echo "$maxscale_err" |grep "ERROR 1045 (28000): Access denied for user 'no_such_user'@'"
if [ "$?" != 0 ]; then
	echo "Maxscale message is not ok!"
    echo "Message: $maxscale_err"
	res=1
else
	echo "Messages are same"
	res=0
fi

exit $res
