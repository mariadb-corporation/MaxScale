#!/bin/bash

script=`basename "$0"`

source=$src_dir/masking/$1/masking_rules.json
target=${maxscale_000_whoami}@${maxscale_000_network}:/home/${maxscale_000_whoami}/masking_rules.json

if [ ${maxscale_000_network} != "127.0.0.1" ] ; then
        scp -i $maxscale_000_keyfile -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $source $target
else
        cp $source /home/${maxscale_000_whoami}/masking_rules.json
fi

if [ $? -ne 0 ]
then
    echo "error: Could not copy rules file to maxscale host."
    exit 1
fi

echo $source copied to $target, restarting maxscale

ssh  -i $maxscale_000_keyfile -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ${maxscale_000_whoami}@${maxscale_000_network} 'sudo service maxscale restart'

test_dir=`pwd`

logdir=log_$1
[ -d $logdir ] && rm -r $logdir
mkdir $logdir || exit 1

# [Read Connection Listener Master] in cnf/maxscale.maxscale.cnf.template.$1
port=4008

dir="$src_dir/masking/$1"

user=skysql
test_name=masking_user
mysqltest --no-defaults \
          --host=${maxscale_000_network} --port=$port \
          --user=$maxscale_user --password=$maxscale_password \
          --logdir=$logdir \
          --test-file=$dir/t/$test_name.test \
          --result-file=$dir/r/"$test_name"_"$user".result \
          --silent
if [ $? -eq 0 ]
then
    echo " OK"
else
    echo " FAILED"
    res=1
fi

user=maxskysql
test_name=masking_user
mysqltest --no-defaults \
          --host=${maxscale_000_network} --port=$port \
          --user=$maxscale_user --password=$maxscale_password \
          --logdir=$logdir \
          --test-file=$dir/t/$test_name.test \
          --result-file=$dir/r/"$test_name"_"$user".result \
          --silent
if [ $? -eq 0 ]
then
    echo " OK"
else
    echo " FAILED"
    res=1
fi

echo $res
