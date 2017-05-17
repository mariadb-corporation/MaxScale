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

$test_dir/non_native_setup $1

cd $2 || exit 1

res=0

# Create a directory for the mysqltest logs
[ -d log ] && rm -r log
mkdir log || exit 1

echo

# Run the test
for t in `cd t; ls *.test`
do
    printf "$t:"
    test_name=${t%%.test}
    mysqltest --host=$maxscale_IP --port=$port \
              --user=$user --password=$password \
              --logdir=log \
              --test-file=t/$test_name.test \
              --result-file=r/$test_name.result \
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
$test_dir/copy_logs.sh $1

exit $res
