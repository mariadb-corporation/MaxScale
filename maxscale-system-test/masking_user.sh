#!/bin/bash

script=`basename "$0"`

if [ $# -lt 1 ]
then
    echo "usage: $script name"
    echo ""
    echo "name    : The name of the test (from CMakeLists.txt) That selects the"
    echo "          configuration template to be used."
    exit 1
fi

if [ "$maxscale_IP" == "" ]
then
    echo "Error: The environment variable maxscale_IP must be set."
    exit 1
fi

source=masking/$1/masking_rules.json
target=vagrant@$maxscale_IP:/home/$maxscale_access_user/masking_rules.json

if [ $maxscale_IP != "127.0.0.1" ] ; then
        scp -i $maxscale_keyfile -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $source $target
else
        cp $source /home/$maxscale_access_user/masking_rules.json
fi

if [ $? -ne 0 ]
then
    echo "error: Could not copy rules file to maxscale host."
    exit 1
fi

echo $source copied to $target

test_dir=`pwd`

$test_dir/non_native_setup $1

cd masking/$1
[ -d log ] && rm -r log
mkdir log || exit 1

# [Read Connection Listener Master] in cnf/maxscale.maxscale.cnf.template.$1
port=4008
password=skysql

user=skysql
test_name=masking_user
mysqltest --host=$maxscale_IP --port=$port \
          --user=$user --password=$password \
          --logdir=log \
          --test-file=t/$test_name.test \
          --result-file=r/"$test_name"_"$user".result \
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
mysqltest --host=$maxscale_IP --port=$port \
          --user=$user --password=$password \
          --logdir=log \
          --test-file=t/$test_name.test \
          --result-file=r/"$test_name"_"$user".result \
          --silent
if [ $? -eq 0 ]
then
    echo " OK"
else
    echo " FAILED"
    res=1
fi

echo

# Copy logs from the VM
$test_dir/copy_logs.sh $1

exit $res
