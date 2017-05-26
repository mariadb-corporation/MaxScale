#!/bin/bash

# $1 - test name
# $2 - time mark (in case of periodic logs copying)

if [ -z $1 ]; then
	echo "Test name missing"
	logs_dir="LOGS/nomane"
else
	if [ -z $2 ]; then
                logs_dir="LOGS/$1"
	else
		logs_dir="LOGS/$1/$2"
	fi
#	rm -rf $logs_dir
fi


echo "Creating log dir in workspace $logs_dir"
mkdir -p $logs_dir
if [ $? -ne 0 ]; then
        echo "Error creating log dir"
fi

export maxscale_sshkey=$maxscale_keyfile
echo "log_dir:         $logs_dir"
echo "maxscale_sshkey: $maxscale_sshkey"
echo "maxscale_IP:     $maxscale_IP"

if [ $maxscale_IP != "127.0.0.1" ] ; then
    ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet $maxscale_access_user@$maxscale_IP "rm -rf logs; mkdir logs; $maxscale_access_sudo cp $maxscale_log_dir/*.log logs/; $maxscale_access_sudo cp /tmp/core* logs; $maxscale_access_sudo chmod 777 -R logs"
    scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet $maxscale_access_user@$maxscale_IP:logs/* $logs_dir
    if [ $? -ne 0 ]; then
	echo "Error copying Maxscale logs"
    fi
    #scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet $maxscale_access_user@$maxscale_IP:/tmp/core* $logs_dir
    #scp  -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet $maxscale_access_user@$maxscale_IP:/root/core* $logs_dir
    scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet $maxscale_access_user@$maxscale_IP:$maxscale_cnf $logs_dir
    chmod a+r $logs_dir/*
else
    sudo cp $maxscale_log_dir/*.log $logs_dir
    sudo cp /tmp/core* $logs_dir
    sudo cp $maxscale_cnf $logs_dir
    sudo chmod a+r $logs_dir/*
fi

if [ -z $logs_publish_dir ] ; then
	echo "logs are in workspace only"
else
	echo "Logs publish dir is $logs_publish_dir"
	rsync -a --no-o --no-g LOGS $logs_publish_dir
fi

