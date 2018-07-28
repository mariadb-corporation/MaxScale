#!/bin/bash

###
## @file mxs791.sh Simple connect test in bash
## - connects to Maxscale, checks that defined in cmd line DB is selected

srcdir=$(dirname $(realpath $0))
export test_dir=`pwd`
export test_name="mxs791_galera.sh"
echo test name is $test_name

$srcdir/mxs791_base.sh

res=$?

$srcdir/copy_logs.sh $test_name
exit $res
