#!/bin/bash

rp=`realpath $0`
export test_dir=`dirname $rp`
export test_name="mxs791_galera.sh"
echo test name is $test_name

$test_dir/mxs791_base.sh

res=$?

$test_dir/copy_logs.sh $test_name
exit $res
