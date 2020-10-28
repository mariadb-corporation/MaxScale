#!/bin/bash

SCRIPT=$(readlink -f "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

user=$maxscale_user
password=$maxscale_password

# Ensure that these are EXACTLY like the corresponding values
# in cnf/maxscale.cnf.template.cache_basic
soft_ttl=5
hard_ttl=10

function run_test
{
    local port=$1
    local test_name=$2

    echo $test_name
    logdir=log_$test_name
    mkdir -p $logdir
    mysqltest --no-defaults \
              --host=${maxscale_000_network} --port=$port \
              --user=$user --password=$password \
              --logdir=$logdir \
              --test-file=$dir/t/$test_name.test \
              --result-file=$dir/r/$test_name.result \
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

export dir="$src_dir/cache/$1"

source=$src_dir/cache/$1/cache_rules.json
target=${maxscale_000_whoami}@${maxscale_000_network}:/home/${maxscale_000_whoami}/cache_rules.json

if [ ${maxscale_000_network} != "127.0.0.1" ] ; then
   scp -i ${maxscale_000_keyfile} -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $source $target
   ssh -i $maxscale_000_keyfile -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ${maxscale_000_whoami}@${maxscale_000_network} chmod a+r /home/${maxscale_000_whoami}/cache_rules.json
else
   cp $source /home/${maxscale_000_whoami}/cache_rules.json
fi

if [ $? -ne 0 ]
then
    echo "error: Could not copy rules file to maxscale host."
    exit 1
fi

echo $source copied to $target, restarting Maxscale

ssh  -i $maxscale_000_keyfile -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ${maxscale_000_whoami}@${maxscale_000_network} 'sudo service maxscale restart'

# We sleep slightly longer than the TTL to ensure that the TTL mechanism
# kicks in.
let seconds=$soft_ttl+2

function run_tests
{
    local port=$1
    local seconds=$2

    run_test $port create || exit 1
    run_test $port insert1 || exit 1
    # We should now get result 1, as this is the first select.
    run_test $port select1 || exit 1

    run_test $port update2 || exit 1
    # We should now get result 1, as ttl has NOT passed.
    run_test $port select1 || exit 1

    echo "Sleeping $seconds"
    sleep $seconds
    # We should now get result 2, as soft ttl has PASSED.
    run_test $port select2 || exit 1

    run_test $port update3 || exit 1
    # We should now get result 2, as ttl has NOT passed.
    run_test $port select2 || exit 1

    echo "Sleeping $seconds"
    sleep $seconds
    # We should now get result 3, as soft ttl has PASSED.
    run_test $port select3 || exit 1

    run_test $port delete || exit 1
    # We should now get result 3, as soft ttl has NOT passed.
    run_test $port select3 || exit 1

    echo "Sleeping $seconds"
    sleep $seconds
    # We should now get result 0, as soft ttl has PASSED.
    run_test $port select0 || exit 1

    # Cleanup
    run_test $port drop || exit 1
}

# Install memcached and redis
${SCRIPTPATH}/cache_install_and_start_storages.sh

# See cnf/maxscale.cnf.template.cache_basic for the ports

echo Testing with local storage
let seconds=$soft_ttl+2
run_tests 4008 $seconds

echo Testing with memcached storage
let seconds=$soft_ttl+2
run_tests 4009 $seconds

echo Testing with redis storage
# Redis does not distinguish between soft/hard TTL
let seconds=$hard_ttl+2
run_tests 4010 $seconds
