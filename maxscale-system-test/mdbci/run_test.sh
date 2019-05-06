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
# '-LE UNSTABLE'
# if $test_set starts from 'NAME#' ctest will not be executed,
# the value of $test_set after 'NAME#' is used as bash command 
# line
# example: '#NAME long_test_time=3600 ./long_test'

export vm_memory=${vm_memory:-"2048"}
export dir=`pwd`

ulimit -n

# read the name of build scripts directory
export script_dir="$(dirname $(readlink -f $0))"

. ${script_dir}/set_run_test_variables.sh

rm -rf LOGS

export target=`echo $target | sed "s/?//g"`
export mdbci_config_name=`echo ${mdbci_config_name} | sed "s/?//g"`

export provider=`mdbci show provider $box --silent 2> /dev/null`
export backend_box=${backend_box:-"centos_7_"$provider}

export sshuser=`mdbci ssh --command 'whoami' --silent $mdbci_config_name/maxscale_000 2> /dev/null | tr -d '\r'`
export IP=`mdbci show network $mdbci_config_name/maxscale_000 --silent 2> /dev/null`
export sshkey=`mdbci show keyfile $mdbci_config_name/maxscale_000 --silent 2> /dev/null | sed 's/"//g'`
export scpopt="-i $sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o ConnectTimeout=120 "
export sshopt="$scpopt $sshuser@$IP"

mdbci destroy ${mdbci_config_name}

. ${script_dir}/configure_log_dir.sh

ulimit -c unlimited

cd ${script_dir}/../../

if [[ "$name" =~ '-gcov' ]]
then
    echo "Building MaxScale from source on maxscale_000"
    rsync -avz --delete -e "ssh $scpopt" ${script_dir}/../../ $sshuser@$IP:/tmp/MaxScale/
    ssh $sshopt "/tmp/MaxScale/BUILD/install_build_deps.sh"
    ssh $sshopt "mkdir /tmp/build && cd /tmp/build && cmake ../MaxScale -DCMAKE_INSTALL_PREFIX=/usr -DGCOV=Y && make && sudo make install"
    ssh $sshopt "sudo chmod -R a+rwx /tmp/build"
    ssh $sshopt "sudo systemctl daemon-reload"
fi

mkdir build && cd build
cmake .. -DBUILD_SYSTEM_TESTS=Y -DBUILDNAME=${mdbci_config_name} -DCMAKE_BUILD_TYPE=Debug
cd maxscale-system-test
make

echo ${test_set} | grep "NAME#"
if [ $? == 0 ] ; then
    named_test=`echo ${test_set} | sed "s/NAME#//"`
    echo ${named_test} | grep "\./"
    if [ $? != 0 ] ; then
        named_test="./"${named_test}
    fi
fi

if [ ! -z "${named_test}" ] ; then
    eval ${named_test}
else
    ctest -VV ${test_set}
fi

if [[ "$name" =~ '-gcov' ]]
then
    ssh $sshopt 'cd /tmp/build && lcov --gcov-tool=$(command -v gcov) -c -d . -o lcov.info && genhtml -o /tmp/gcov-report/ lcov.info'
    rsync -avz --delete -e "ssh $scpopt" $sshuser@$IP:/tmp/gcov-report/ ./${logs_publish_dir}
fi

cp core.* ${logs_publish_dir}
${script_dir}/copy_logs.sh
cd $dir

if [ "${do_not_destroy_vm}" != "yes" ] ; then
	mdbci destroy ${mdbci_config_name}
	echo "clean up done!"
fi
