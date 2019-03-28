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

echo "log_dir:         $logs_dir"
echo "maxscale_sshkey: $maxscale_000_keyfile"
echo "maxscale_IP:     $maxscale_000_network"

if [ $maxscale_IP != "127.0.0.1" ] ; then
    ssh -i ${maxscale_000_keyfile} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet ${maxscale_000_whoami}@${maxscale_000_network} "rm -rf logs; mkdir logs; ${maxscale_000_access_sudo} cp ${maxscale_log_dir}/*.log logs/; ${maxscale_000_access_sudo} cp /tmp/core* logs; ${maxscale_000_access_sudo} chmod 777 -R logs"
    scp -i ${maxscale_000_keyfile} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet ${maxscale_000_whoami}@${maxscale_000_network}:logs/* $logs_dir
    if [ $? -ne 0 ]; then
	echo "Error copying Maxscale logs"
    fi
    scp -i ${maxscale_000_keyfile} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet ${maxscale_000_whoami}@${maxscale_000_network}:$maxscale_cnf $logs_dir
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

for i in `find $logs_dir -name 'core*'`
do
    test -e $i && echo "Test failed: core files generated" && exit 1
done
