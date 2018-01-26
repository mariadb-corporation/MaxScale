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
export name="$box-$product-$version-permanent"

export snapshot_name=${snapshot_name:-"clean"}

rm -rf LOGS

export target=`echo $target | sed "s/?//g"`
export name=`echo $name | sed "s/?//g"`

. ${script_dir}/configure_log_dir.sh

# Setting snapshot_lock
export snapshot_lock_file=${MDBCI_VM_PATH}/${name}_snapshot_lock
if [ -f ${snapshot_lock_file} ]; then
    echo "Snapshot is locked, waiting ..."
fi
while [ -f ${snapshot_lock_file} ]
do
	sleep 5
done

touch ${snapshot_lock_file}
echo $JOB_NAME-$BUILD_NUMBER >> ${snapshot_lock_file}

export repo_dir=$dir/repo.d/

${mdbci_dir}/mdbci snapshot revert --path-to-nodes $name --snapshot-name $snapshot_name

if [ $? != 0 ]; then
	${mdbci_dir}/mdbci destroy $name
	${MDBCI_VM_PATH}/scripts/clean_vms.sh $name

	${script_dir}/create_config.sh
        checkExitStatus $? "Error creating configuration" $snapshot_lock_file
        . ${script_dir}/configure_backend.sh
    
	echo "Creating snapshot from new config"
	${mdbci_dir}/mdbci snapshot take --path-to-nodes $name --snapshot-name $snapshot_name
fi

. ${script_dir}/set_env.sh "$name"

${mdbci_dir}/repository-config/maxscale-ci.sh $target repo.d


${mdbci_dir}/mdbci sudo --command 'yum remove maxscale -y' $name/maxscale
${mdbci_dir}/mdbci sudo --command 'yum clean all' $name/maxscale

${mdbci_dir}/mdbci setup_repo --product maxscale $name/maxscale --repo-dir $repo_dir 
${mdbci_dir}/mdbci install_product --product maxscale $name/maxscale --repo-dir $repo_dir

checkExitStatus $? "Error installing Maxscale" $snapshot_lock_file

cd ${script_dir}/..

cmake . -DBUILDNAME=$JOB_NAME-$BUILD_NUMBER-$target
make

./check_backend --restart-galera

checkExitStatus $? "Failed to check backends" $snapshot_lock_file

ctest $test_set -VV -D Nightly

${script_dir}/copy_logs.sh

# Removing snapshot_lock
rm ${snapshot_lock_file}

