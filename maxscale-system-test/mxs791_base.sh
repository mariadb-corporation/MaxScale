#!/bin/bash
$test_dir/non_native_setup $test_name

if [ $? -ne 0 ] ; then
        echo "configuring maxscale failed"
        exit 1
fi
export ssl_options="--ssl-cert=$test_dir/ssl-cert/client-cert.pem --ssl-key=$test_dir/ssl-cert/client-key.pem"

res=0
echo "Trying RWSplit"
echo "show tables" | mysql -u$maxscale_user -p$maxscale_password -h $maxscale_IP -P 4006 $ssl_option test
if [ $? != 0 ] ; then
        res=1
        echo "Can't connect to DB 'test'"
fi

echo "Trying ReadConn master"
echo "show tables" | mysql -u$maxscale_user -p$maxscale_password -h $maxscale_IP -P 4008 $ssl_options test
if [ $? != 0 ] ; then
        res=1
        echo "Can't connect to DB 'test'"
fi

echo "Trying ReadConn slave"
echo "show tables" | mysql -u$maxscale_user -p$maxscale_password -h $maxscale_IP -P 4009 $ssl_options test
if [ $? != 0 ] ; then
        res=1
        echo "Can't connect to DB 'test'"
fi

exit $res
