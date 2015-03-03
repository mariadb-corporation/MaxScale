#!/bin/bash

export test_name=bug561
$test_dir/configure_maxscale.sh

mariadb_err=`mysql -u$repl_user -p$repl_password -h $repl_000 non_existing_db 2>&1`
maxscale_err=`mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4006 non_existing_db 2>&1`

maxscale_err1=`mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4008 non_existing_db 2>&1`
maxscale_err2=`mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4009 non_existing_db 2>&1`

echo "MariaDB message"
echo "$mariadb_err"
echo " "
echo "Maxscale message from RWSplit"
echo "$maxscale_err"
echo "Maxscale message from ReadConn master"
echo "$maxscale_err1"
echo "Maxscale message from ReadConn slave"
echo "$maxscale_err2"

res=0

#echo "$maxscale_err" | grep "$mariadb_err"
if [ "$maxscale_err" != "$mariadb_err" ] ; then
	echo "Messages are different!"
	res=1
else
	echo "Messages are same"
fi

if [ "$maxscale_err1" != "$mariadb_err" ] ; then
        echo "Messages are different!"
        res=1
else
        echo "Messages are same"
fi

if [ "$maxscale_err2" != "$mariadb_err" ] ; then
        echo "Messages are different!"
        res=1
else
        echo "Messages are same"
fi

$test_dir/copy_logs.sh bug561
exit $res
