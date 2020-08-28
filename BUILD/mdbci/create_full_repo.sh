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

export platform=`${mdbci_dir}/mdbci show boxinfo --box-name=$box --field='platform' --silent`
export platform_version=`${mdbci_dir}/mdbci show boxinfo --box-name=$box --field='platform_version' --silent`
export dist_sfx="$platform"."$platform_version"

# prerare VM
export provider=`${mdbci_dir}/mdbci show provider $box --silent 2> /dev/null`
export name="$box-${JOB_NAME}-${BUILD_NUMBER}"
export name=`echo $name | sed "s|/|-|g"`


# destroying existing box
if [ -d "$MDBCI_VM_PATH/${name}" ]; then
	${mdbci_dir}/mdbci destroy --force $name
fi

  eval "cat <<EOF
$(<${script_dir}/templates/build.json.template)
" 2> /dev/null > $MDBCI_VM_PATH/${name}.json

# starting VM for build
echo "Generating build VM template"
${mdbci_dir}/mdbci --override --template $MDBCI_VM_PATH/$name.json generate $name
echo "starting VM for build"
${mdbci_dir}/mdbci up --attempts=1 $name
if [ $? != 0 ] ; then
	echo "Error starting VM"
	exit 1
fi
echo "copying public keys to VM"
cp  ~/build-scripts/team_keys .
${mdbci_dir}/mdbci public_keys --key team_keys --silent $name


echo "Get VM info"
export sshuser=`${mdbci_dir}/mdbci ssh --command 'whoami' --silent $name/build 2> /dev/null | tr -d '\r'`
export IP=`${mdbci_dir}/mdbci show network $name/build --silent 2> /dev/null`
export sshkey=`${mdbci_dir}/mdbci show keyfile $name/build --silent 2> /dev/null | sed 's/"//g'`
export scpopt="-i $sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o ConnectTimeout=120 "
export sshopt="$scpopt $sshuser@$IP"

rm -rf $pre_repo_dir/$target/$box
mkdir -p $pre_repo_dir/$target/SRC
mkdir -p $pre_repo_dir/$target/$box

export work_dir="MaxScale"
export orig_image=$box

ssh $sshopt "sudo rm -rf $work_dir"
echo "copying stuff to $image machine"
ssh $sshopt "mkdir -p $work_dir"

rsync -avz --delete -e "ssh $scpopt" ${script_dir}/../../ $sshuser@$IP:./$work_dir/
if [ $? -ne 0 ] ; then
  echo "Error copying stuff to $box machine"
  exit 2
fi

ssh $sshopt ./MaxScale/BUILD/install_build_deps.sh
${script_dir}/create_remote_repo.sh full_repo
export build_result=$?

if [ ${build_result} -eq 0 ]; then
        ${script_dir}/copy_repos.sh
        export build_result=$?
fi

echo "Removing locks and destroying VM"

if [[ "$do_not_destroy_vm" != "yes" && "$try_already_running" != "yes" ]] ; then
  echo "Destroying VM"
  ${mdbci_dir}/mdbci destroy --force $name
fi
cd $dir

if [ $build_result -ne 0 ] ; then
        echo "Build FAILED!"
        exit $build_result
fi

