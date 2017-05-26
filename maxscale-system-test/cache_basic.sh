#!/bin/bash

user=skysql
password=skysql

# See cnf/maxscale.cnf.template.cache_basic
port=4008
# Ensure that these are EXACTLY like the corresponding values
# in cnf/maxscale.cnf.template.cache_basic
soft_ttl=5
hard_ttl=10

function run_test
{
    local test_name=$1

    echo $test_name

    mysqltest --host=$maxscale_IP --port=$port \
              --user=$user --password=$password \
              --logdir=log \
              --test-file=t/$test_name.test \
              --result-file=r/$test_name.result \
              --silent

    if [ $? -eq 0 ]
    then
        echo " OK"
	rc=0
    else
        echo " FAILED"
        rc=1
    fi

    return $rc
}

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

expected_name="cache_basic"

if [ "$1" != "$expected_name" ]
then
    echo "warning: Expected test name to be $expected_name_basic, was $1."
fi

source=cache/$1/cache_rules.json
target=vagrant@$maxscale_IP:/home/$maxscale_access_user/cache_rules.json

if [ $maxscale_IP != "127.0.0.1" ] ; then
   scp -i $maxscale_keyfile -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $source $target
else
   cp $source /home/$maxscale_access_user/cache_rules.json
fi

if [ $? -ne 0 ]
then
    echo "error: Could not copy rules file to maxscale host."
    exit 1
fi

echo $source copied to $target

test_dir=`pwd`

$test_dir/non_native_setup $1

cd cache/$expected_name

[ -d log ] && rm -r log
mkdir log || exit 1

echo

# We sleep slightly longer than the TTL to ensure that the TTL mechanism
# kicks in.
let seconds=$soft_ttl+2

run_test create || exit 1
run_test insert1 || exit 1
# We should now get result 1, as this is the first select.
run_test select1 || exit 1

run_test update2 || exit 1
# We should now get result 1, as ttl has NOT passed.
run_test select1 || exit 1

echo "Sleeping $seconds"
sleep $seconds
# We should now get result 2, as soft ttl has PASSED.
run_test select2 || exit 1

run_test update3 || exit 1
# We should now get result 2, as ttl has NOT passed.
run_test select2 || exit 1

echo "Sleeping $seconds"
sleep $seconds
# We should now get result 3, as soft ttl has PASSED.
run_test select3 || exit 1

run_test delete || exit 1
# We should now get result 3, as soft ttl has NOT passed.
run_test select3 || exit 1

echo "Sleeping $seconds"
sleep $seconds
# We should now get result 0, as soft ttl has PASSED.
run_test select0 || exit 1

# Cleanup
run_test drop || exit 1
