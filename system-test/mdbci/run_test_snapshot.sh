#!/bin/bash

function checkExitStatus {
    returnCode=$1
    errorMessage=$2
    lockFilePath=$3
    if [ "$returnCode" != 0 ]; then
        echo "$errorMesage"
        rm $lockFilePath
        echo "Snapshot lock file was deleted due to an error"
        exit 1
    fi
}

set -x
export dir=`pwd`

# read the name of build scripts directory
export script_dir="$(dirname $(readlink -f $0))"

. ${script_dir}/set_run_test_variables.sh
export mdbci_config_name="$box-$product-$version-permanent"

export snapshot_name=${snapshot_name:-"clean"}

rm -rf LOGS

export target=`echo $target | sed "s/?//g"`
export mdbci_config_name=`echo ${mdbci_config_name} | sed "s/?//g"`

. ${script_dir}/configure_log_dir.sh

# Setting snapshot_lock
export snapshot_lock_file=${MDBCI_VM_PATH}/${mdbci_config_name}_snapshot_lock
if [ -f ${snapshot_lock_file} ]; then
    echo "Snapshot is locked, waiting ..."
fi
while [ -f ${snapshot_lock_file} ]
do
	sleep 5
done

touch ${snapshot_lock_file}
echo $JOB_NAME-$BUILD_NUMBER >> ${snapshot_lock_file}

mdbci snapshot revert --path-to-nodes ${mdbci_config_name} --snapshot-name ${snapshot_name}

if [ $? != 0 ]; then
	mdbci destroy --force ${mdbci_config_name}
	${MDBCI_VM_PATH}/scripts/clean_vms.sh ${mdbci_config_name}

	new_config=true	

fi

for maxscale_vm_name in ${maxscales_vm}
do

    checkExitStatus $? "Error installing Maxscale" $snapshot_lock_file
done


cd ${script_dir}/../../

rm -rf build

mkdir build && cd build
cmake .. -DBUILDNAME=$JOB_NAME-$BUILD_NUMBER-$target -DBUILD_SYSTEM_TESTS=Y -DCMAKE_BUILD_TYPE=Debug
cd system-test
make

./check_backend --restart-galera --reinstall-maxscale
checkExitStatus $? "Failed to check backends" $snapshot_lock_file

if [${new_config}] == "true" ; then
	echo "Creating snapshot from new config"
	mdbci snapshot take --path-to-nodes ${mdbci_config_name} --snapshot-name $snapshot_name
fi


ulimit -c unlimited
ctest $test_set -VV
ctest --rerun-failed -VV
cp core.* ${logs_publish_dir}
${script_dir}/copy_logs.sh


# Removing snapshot_lock
rm ${snapshot_lock_file}

