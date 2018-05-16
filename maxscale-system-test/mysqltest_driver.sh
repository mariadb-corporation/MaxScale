#!/bin/bash

# First argument is the name of the test
# Second argument is the directory name where tests are found
# Third argument defines the MaxScale port
# Fourth OPTIONAL argument defines the user to be used.
# Fifth OPTIONAL argument defines the password to be used.

if [ $# -lt 3 ]
then
    echo "Usage: NAME TESTDIR PORT [USER] [PASSWORD]"
    exit 1
fi

if [ "$maxscale_IP" == "" ]
then
    echo "Error: The environment variable maxscale_IP must be set."
    exit 1
fi

if [ $# -ge 5 ]
then
    password=$5
else
    password=skysql
fi

if [ $# -ge 4 ]
then
    user=$4
else
    user=skysql
fi

# Prepare the test environment
test_dir=`pwd`
port=$3

res=0

# Create a directory for the mysqltest logs
[ -d log_$1 ] && rm -r log_$1
mkdir log_$1

echo

# Run the test
for t in `$2/t/*.test|xargs -L 1 basename`
do
    printf "$t:"
    test_name=${t%%.test}
    mysqltest --host=$maxscale_IP --port=$port \
              --user=$user --password=$password \
              --logdir=log_$1 \
              --test-file=$2/t/$test_name.test \
              --result-file=$2/r/$test_name.result \
              --silent

    if [ $? -eq 0 ]
    then
        echo " OK"
    else
        echo " FAILED"
        res=1
    fi
done

echo

# Copy logs from the VM
$src_dir/copy_logs.sh $1

exit $res
