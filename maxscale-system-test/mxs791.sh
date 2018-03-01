#!/bin/bash

###
## @file mxs791.sh Simple connect test in bash
## - connects to Maxscale, checks that defined in cmd line DB is selected

rp=`realpath $0`
export test_dir=`dirname $rp`
export test_name="mxs791.sh"
echo test name is $test_name

$test_dir/mxs791_base.sh

res=$?

$test_dir/copy_logs.sh $test_name
exit $res
