#!/bin/bash

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

# $ci_url - URL to Maxscale CI repository
# (default "http://max-tst-01.mariadb.com/ci-repository/")
# if build is done also locally and binaries are not uploaded to
# max-tst-01.mariadb.com $ci_url should toint to local web server
# e.g. http://192.168.122.1/repository (IP should be a host IP in the
# virtual network (not 127.0.0.1))

# $product - 'mariadb' or 'mysql'

# $version - version of backend DB (e.g. '10.1', '10.2')

# $galera_version - version of Galera backend DB
# same as $version by default

# $target - name of binary repository
# (name of subdirectroy http://max-tst-01.mariadb.com/ci-repository/)

# $team_keys - path to the file with open ssh keys to be
# installed on all VMs (default ${HOME}/team_keys)

# $don_not_destroy_vm - if 'yes' VM won't be destored afther the test

# $test_set - parameters to be send to 'ctest' (e.g. '-I 1,100',
# '-LE UNSTABLE'

export vm_memory=${vm_memory:-"2048"}
export dir=`pwd`

ulimit -n

# read the name of build scripts directory
export script_dir="$(dirname $(readlink -f $0))"

. ${script_dir}/set_run_test_variables.sh

rm -rf LOGS

export target=`echo $target | sed "s/?//g"`
export name=`echo $name | sed "s/?//g"`

. ${script_dir}/configure_log_dir.sh

${script_dir}/create_config.sh
res=$?

ulimit -c unlimited
if [ $res == 0 ] ; then
    . ${script_dir}/set_env.sh $name
    cd ${script_dir}/../../

    mkdir build && cd build
    cmake .. -DBUILD_SYSTEM_TESTS=Y -DBUILDNAME=$name -DCMAKE_BUILD_TYPE=Debug
    cd maxscale-system-test
    make
set -x
    echo ${test_set} | grep "NAME#"
    if [ $? == 0 ] ; then
        named_test=`echo ${test_set} | sed "s/NAME#//" | sed "s/ //g"`
    fi

    if [ ! -z "${named_test}" ] ; then
        ./${named_test}
    else
        ./check_backend
        if [ $? != 0 ]; then
            echo "Backend broken!"
            if [ "${do_not_destroy_vm}" != "yes" ] ; then
                ${mdbci_dir}/mdbci destroy $name
            fi
            rm ~/vagrant_lock
            exit 1
        fi
        ${mdbci_dir}/mdbci snapshot take --path-to-nodes $name --snapshot-name clean
        ctest -VV ${test_set}
    fi
    cp core.* ${logs_publish_dir}
    ${script_dir}/copy_logs.sh
    cd $dir
else
  echo "Failed to create VMs, exiting"
  if [ "${do_not_destroy_vm}" != "yes" ] ; then
	${mdbci_dir}/mdbci destroy $name
  fi
  rm ~/vagrant_lock
  exit 1
fi

if [ "${do_not_destroy_vm}" != "yes" ] ; then
	${mdbci_dir}/mdbci destroy $name
	echo "clean up done!"
fi
