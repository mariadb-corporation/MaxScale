#!/bin/bash

script=`basename "$0"`

if [ $# -lt 1 ]
then
    echo "usage: $script name [user] [password]"
    echo ""
    echo "name    : The name of the test (from CMakeLists.txt) That selects the"
    echo "          configuration template to be used."
    echo "user    : The user using which the test should be run."
    echo "password: The password of the user."
    exit 1
fi

if [ "$maxscale_IP" == "" ]
then
    echo "Error: The environment variable maxscale_IP must be set."
    exit 1
fi

source=masking/$1/masking_rules.json
target=$maxscale_access_user@$maxscale_IP:/home/$maxscale_access_user/masking_rules.json

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

password=
if [ $# -ge 3 ]
then
    password=$3
fi

user=
if [ $# -ge 2 ]
then
    user=$2
fi

# [Read Connection Listener Master] in cnf/maxscale.maxscale.cnf.template.$1
port=4008

./mysqltest_driver.sh $1 ./masking/$1 $port $user $password
