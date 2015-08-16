#!/bin/bash

function check_server_count()
{
    maxadmin_output=$(ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$maxdir_bin/maxadmin -p$maxadmin_password -uadmin list servers")

    server_count=$(echo "$maxadmin_output1"|grep -i 'server'|wc -l)
    if [[ $server_count -ne $1 ]]
    then
        echo "Error: Incorrect amount of servers!"
        echo "Found $server_count servers when $1 was expected"
        export maxscale_restart=$old_val
        return 1
    fi
    return 0
}

# Save the old configuration value
old_val=$maxscale_restart
export maxscale_restart=no

# Start with a 4 server config file
export test_name=config_reload
$test_dir/configure_maxscale.sh
echo "Waiting for 15 seconds"
sleep 15

if [[ check_server_count(4) -ne 0 ]]
then
    exit 1
fi

# Reload the same config file but with one extra server
# and check that the new server is detected
export test_name=config_reload.add_server
$test_dir/configure_maxscale.sh
echo "Waiting for 15 seconds"
sleep 15

# 
if [[ check_server_count(5) -ne 0 ]]
then
    exit 1
fi

# Remove the extra server and check that it is also
# removed from MaxScale
export test_name=config_reload
$test_dir/configure_maxscale.sh
echo "Waiting for 15 seconds"
sleep 15

if [[ check_server_count(4) -ne 0 ]]
then
    exit 1
fi

export maxscale_restart=$old_val
