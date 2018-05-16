#!/bin/bash

rp=`realpath $0`
export src_dir=`dirname $rp`

./non_native_setup insertstream

$src_dir/mysqltest_driver.sh insertstream $src_dir/insertstream 4006
