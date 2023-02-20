#!/bin/bash

# Creates RPM or DEB repository for binaries from
# $pre_repo_dir/$target/$box, signs it with keys
# from ${gpg_keys_path} and puts signed repo to

set -x

export work_dir="MaxScale"

echo "creating repository"
echo "cleaning VM"
ssh $sshopt "rm -rf dest; rm -rf src;"

echo " creating dirs on VM"
ssh $sshopt "mkdir -p dest ; mkdir -p src; mkdir gpg_keys"

echo "copying stuff to VM"
if [ $1 == "full_repo" ] ; then
         find  ${repo_path}/maxscale-${major_ver}.*-release/mariadb-maxscale/${platform}/${platform_version}/* -name "*.rpm" -exec scp $scpopt {} $sshuser@$IP:src/ \;
         find  ${repo_path}/maxscale-${major_ver}.*-release/mariadb-maxscale/${platform}/dists/${platform_version}/* -name "*.deb" -exec scp $scpopt {} $sshuser@$IP:src/ \;
else
         scp $scpopt $pre_repo_dir/$target/$box/* $sshuser@$IP:src/
fi

scp $scpopt -r ${gpg_keys_path}/* $sshuser@$IP:./gpg_keys/
ssh $sshopt "key=\`ls ~/gpg_keys/*.public -1\` ; gpg --import \$key"
ssh $sshopt "key=\`ls ~/gpg_keys/*.private -1\` ; gpg --allow-secret-key-import --import \$key"

echo "executing create_repo.sh on VM"
ssh $sshopt "export platform=$platform; export platform_version=$platform_version; ./$work_dir/BUILD/mdbci/create_repo.sh dest/ src/"
if [ $? != 0 ] ; then
	echo "Repo creation failed!"
	exit 1
fi

echo "cleaning ${unsorted_repo_dir}/$target/$box"
rm -rf ${unsorted_repo_dir}/$target/$box
echo "cleaning ${pre_repo_dir}/$target/$box"
rm -rf ${pre_repo_dir}/$target/$box

echo "copying repo from $box"
mkdir -p ${unsorted_repo_dir}/$target/$box
scp $scpopt -r $sshuser@$IP:dest/* ${unsorted_repo_dir}/$target/$box/
