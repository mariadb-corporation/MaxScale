#!/bin/bash


user=$maxscale_user
password=$maxscale_password

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
