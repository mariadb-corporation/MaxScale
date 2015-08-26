#!/bin/bash

function check_service_count()
{
    maxadmin_output=$(ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$maxdir_bin/maxadmin -p$maxadmin_password -uadmin list servers")
    echo "$maxadmin_output"
    service_count=$(echo "$maxadmin_output"|grep -i 'server[1-5]'|wc -l)
    if [[ $service_count -ne $1 ]]
    then
        echo "Error: Incorrect amount of services!"
        echo "Found $service_count servers when $1 was expected"
        export maxscale_restart=$old_val
        return 1
    fi
    return 0
}

# Save the old configuration value
old_val=$maxscale_restart
export maxscale_restart=no

# Start with a 2 service config file
export test_name=config_reload
$test_dir/configure_maxscale.sh
echo "Waiting for 15 seconds"
sleep 15

check_service_count 2
if [[ $? -ne 0 ]]
then
    exit 1
fi

# Test that all services work
mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4006 -e "select 1"
if [[ $? -ne 0 ]]
then
    echo "Error: Failed to execute query on service listening at $maxscale_IP:4006"
    exit 1
fi

# Reload the same config file but with new service
# and check that the new service is detected.
export test_name=reload_service
$test_dir/configure_maxscale.sh
echo "Waiting for 15 seconds"
sleep 15

check_service_count 3
if [[ $?  -ne 0 ]]
then
    exit 1
fi

# Test that all services still work after the reload.
mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4006 -e "select 1"
if [[ $? -ne 0 ]]
then
    echo "Error: Failed to execute query on service listening at $maxscale_IP:4006"
    exit 1
fi

mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4008 -e "select 1"
if [[ $? -ne 0 ]]
then
    echo "Error: Failed to execute query on service listening at $maxscale_IP:4008"
    exit 1
fi

# Remove the extra server and check that it is also
# removed from MaxScale
export test_name=config_reload
$test_dir/configure_maxscale.sh
echo "Waiting for 15 seconds"
sleep 15

check_service_count 2
if [[ $? -ne 0 ]]
then
    exit 1
fi

# Test that the remaining service works and that the previously added service
# is removed.

mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4006 -e "select 1"
if [[ $? -ne 0 ]]
then
    echo "Error: Failed to execute query on service listening at $maxscale_IP:4006"
    exit 1
fi

mysql -u$repl_user -p$repl_password -h $maxscale_IP -P 4008 -e "select 1"
if [[ $? -eq 0 ]]
then
    echo "Error: Successfully execute query on service listening at $maxscale_IP:4008"
    echo "This service should have been disabled"
    exit 1
fi
export maxscale_restart=$old_val
