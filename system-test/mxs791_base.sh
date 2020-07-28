#!/bin/bash

rp=`realpath $0`

export ssl_options="--ssl-cert=$src_dir/ssl-cert/client-cert.pem --ssl-key=$src_dir/ssl-cert/client-key.pem"

res=0
echo "Trying RWSplit"
echo "show tables" | mysql -u$maxscale_user -p$maxscale_password -h ${maxscale_000_network} -P 4006 $ssl_option test
if [ $? != 0 ] ; then
        res=1
        echo "Can't connect to DB 'test'"
fi

echo "Trying ReadConn master"
echo "show tables" | mysql -u$maxscale_user -p$maxscale_password -h ${maxscale_000_network} -P 4008 $ssl_options test
if [ $? != 0 ] ; then
        res=1
        echo "Can't connect to DB 'test'"
fi

echo "Trying ReadConn slave"
echo "show tables" | mysql -u$maxscale_user -p$maxscale_password -h ${maxscale_000_network} -P 4009 $ssl_options test
if [ $? != 0 ] ; then
        res=1
        echo "Can't connect to DB 'test'"
fi

exit $res
