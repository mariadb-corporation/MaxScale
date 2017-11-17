#!/bin/bash

# $box - Vagrant box to be used for build

# $target - name of repository to put results

# $cmake_flags - cmake flags to be used in the build

# $MDBCI_VM_PATH - path to the MDBCI virtual machies directory

# $source - reference to the point in the source code repository

# $do_not_destroy_vm - if "yes" VM stays alive after the build

# $try_already_running - if "yes" already running VM will be used for build

# $gpg_keys_path - path to the directory containing GPG keys for repo signing
# directory have to contain only one file *.public and only one *.private

set -x

# read the name of build scripts directory
export script_dir="$(dirname $(readlink -f $0))"

# load all needed variables
. ${script_dir}/set_build_variables.sh

dist_sfx="$platform"."$platform_version"
export cmake_flags="${cmake_flags} -DPACKAGE=Y -DDISTRIB_SUFFIX=${dist_sfx}"

# prerare VM
export provider=`${mdbci_dir}/mdbci show provider $box --silent 2> /dev/null`
export name="$box-${JOB_NAME}-${BUILD_NUMBER}"
export name=`echo $name | sed "s|/|-|g"`

export platform=`${mdbci_dir}/mdbci show boxinfo --box-name=$box --field='platform' --silent`
export platform_version=`${mdbci_dir}/mdbci show boxinfo --box-name=$box --field='platform_version' --silent`


if [ "${try_already_running}" == "yes" ]; then
  export name=${box}
  export snapshot_lock_file=$MDBCI_VM_PATH/${name}_snapshot_lock
  while [ -f ${snapshot_lock_file} ]
  do
    echo "snapshot is locked, waiting ..."
    sleep 5
  done
  echo ${JOB_NAME}-${BUILD_NUMBER} > ${snapshot_lock_file}
  ${mdbci_dir}/mdbci snapshot revert --path-to-nodes $name --snapshot-name clean
  if [ $? == 0 ]; then
    export already_running="ok"
  fi
fi

if [ "$already_running" != "ok" ]; then

  eval "cat <<EOF
$(<${script_dir}/templates/build.json.template)
" 2> /dev/null > $MDBCI_VM_PATH/${name}.json

	while [ -f ~/vagrant_lock ]
	do
		sleep 5
	done
	touch ~/vagrant_lock
	echo $JOB_NAME-$BUILD_NUMBER >> ~/vagrant_lock

	# destroying existing box
	if [ -d "$MDBCI_VM_PATH/${name}" ]; then
		cd $MDBCI_VM_PATH/${name}
		vagrant destroy -f
		cd ${dir}
	fi

	# starting VM for build
	echo "Generating build VM template"
	${mdbci_dir}/mdbci --override --template $MDBCI_VM_PATH/$name.json generate $name
	echo "starting VM for build"
	${mdbci_dir}/mdbci up --attempts=1 $name
	if [ $? != 0 ] ; then
		echo "Error starting VM"
		cd $MDBCI_VM_PATH/${name}
		rm ~/vagrant_lock
		cd $dir
		exit 1
	fi
	echo "copying public keys to VM"
	cp  ~/build-scripts/team_keys .
	${mdbci_dir}/mdbci public_keys --key team_keys --silent $name
fi

echo "Get VM info"
export sshuser=`${mdbci_dir}/mdbci ssh --command 'whoami' --silent $name/build 2> /dev/null | tr -d '\r'`
export IP=`${mdbci_dir}/mdbci show network $name/build --silent 2> /dev/null`
export sshkey=`${mdbci_dir}/mdbci show keyfile $name/build --silent 2> /dev/null | sed 's/"//g'`
export scpopt="-i $sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o ConnectTimeout=120 "
export sshopt="$scpopt $sshuser@$IP"

echo "Release Vagrant lock"
rm ~/vagrant_lock

echo "Starting build"
${script_dir}/remote_build.sh
export build_result=$?

shellcheck `find . | grep "\.sh"` | grep -i "POSIX sh"
if [ $? -eq 0 ] ; then
        echo "POSIX sh error are found in the scripts"
#        exit 1
fi


${script_dir}/create_remote_repo.sh

${script_dir}/copy_repos.sh


echo "Removing locks and destroying VM"
cd $MDBCI_VM_PATH/$name
if [ "$try_already_running" == "yes" ] ; then
  echo "Release lock for already running VM"
  rm $snapshot_lock_file
fi
if [[ "$do_not_destroy_vm" != "yes" && "$try_already_running" != "yes" ]] ; then
  echo "Destroying VM"
  vagrant destroy -f
  cd ..
  rm -rf $name
  rm -rf ${name}.json
  rm -rf ${name}_netwotk_config
fi
cd $dir

if [ $build_result -ne 0 ] ; then
        echo "Build FAILED!"
        exit $build_result
fi

