#!/bin/bash
#
# Copyright (c) 2016 MariaDB Corporation Ab
# Copyright (c) 2023 MariaDB plc, Finnish Branch
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2027-04-10
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

# see set_run_test_variables.sh for default values of all variables

# $box - Name of Vagrant box for Maxscale machine
# see lists of supported boxes
# https://github.com/mariadb-corporation/mdbci/tree/integration/BOXES

# $template - name of MDBCI json template file
# Template file have to be in ./templates/, file name
# have to be '$template.json.template
# Template file can contain references to any variables -
# all ${variable_name} will be replaced with values

# $name - name of test run. It can be any string of leytters or digits
# If it is not defined, name will be automatically genereted
# using $box and current date and time

# $product - 'mariadb' or 'mysql'

# $version - version of backend DB (e.g. '10.1', '10.2')

# $galera_version - version of Galera backend DB
# same as $version by default

# $target - name of binary repository
# (name of subdirectroy http://max-tst-01.mariadb.com/ci-repository/)

# $team_keys - path to the file with open ssh keys to be
# installed on all VMs (default ${HOME}/team_keys)

# $do_not_destroy_vm - if 'yes' VM won't be destored afther the test

# $test_set - parameters to be send to 'ctest' (e.g. '-I 1,100',
# '-LE HEAVY'
# if $test_set starts from 'NAME#' ctest will not be executed,
# the value of $test_set after 'NAME#' is used as bash command 
# line
# example: '#NAME long_test_time=3600 ./long_test'
#
# $maxscale_product - use CI or production version of Maxscale
# 'maxscale_ci' or 'maxscale'

export vm_memory=${vm_memory:-"2048"}
export dir=`pwd`

# read the name of build scripts directory
export script_dir="$(dirname $(readlink -f $0))"

. ${script_dir}/set_run_test_variables.sh

rm -rf LOGS

export target=`echo $target | sed "s/?//g"`
export mdbci_config_name=`echo ${mdbci_config_name} | sed "s/?//g"`

export provider=`mdbci show provider $box --silent 2> /dev/null`
export backend_box=${backend_box:-"rocky_8_"$provider}
export maxscale_product=${maxscale_product:-"maxscale_ci"}

mdbci destroy --force ${mdbci_config_name}

. ${script_dir}/configure_log_dir.sh

ulimit -c unlimited

cd ${script_dir}/../../

rm -rf build
mkdir build && cd build
cmake .. -DBUILD_SYSTEM_TESTS=Y -DBUILDNAME=${mdbci_config_name} -DCMAKE_BUILD_TYPE=Debug
cd system-test
NCPU=$(grep -c processor /proc/cpuinfo)
make -j $NCPU
if [ $? != 0 ] ; then
    echo "Test code build FAILED! exiting"
    exit 1
fi

echo ${test_set} | grep "NAME#"
if [ $? == 0 ] ; then
    named_test=`echo ${test_set} | sed "s/NAME#//"`
    echo ${named_test} | grep "\./"
    if [ $? != 0 ] ; then
        named_test="./"${named_test}
    fi
fi

# Build MaxScale locally on the VM
if [[ "$name" =~ '-gcov' ]]
then
    echo "Building MaxScale from source on maxscale_000"
    MXS_BRANCH="$test_branch" ./gcov_start
fi

if [ ! -z "${named_test}" ] ; then
    eval ${named_test}
else
    eval "arguments=(${test_set})"

    if [ $? -ne 0 ]
    then
        echo "Invalid test set!"
        exit 1
    fi

    ctest -N "${arguments[@]}"
    ctest -VV "${arguments[@]}" --test-output-size-passed 16777216 --test-output-size-failed 16777216 --output-junit LOGS/maxscale.xml --verbose --overwrite BuildName=maxscale
fi

if [[ "$name" =~ '-gcov' ]]
then
     ./gcov_stop
fi

cp core.* ${logs_publish_dir}
${script_dir}/copy_logs.sh
cd $dir

if [ "${do_not_destroy_vm}" != "yes" ] ; then
	mdbci destroy --force ${mdbci_config_name}
	echo "clean up done!"
fi
