#!/bin/bash

# Copyies stuff to VM, run build on VM and copies binaries 
# to $pre_repo_dir/$target/$box

set -x


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

export install_script="install_build_deps.sh"

if [ "$box_type" == "RPM" ] ; then
  build_script="build_rpm_local.sh"
  files="*.rpm"
  tars="$product_name*.tar.gz"
else
  build_script="build_deb_local.sh"
  files="../*.deb"
  tars="$product_name*.tar.gz"
fi

export remote_build_cmd="export already_running=\"$already_running\"; \
  export build_experimental=\"$build_experimental\"; \
  export cmake_flags=\"$cmake_flags\"; \
  export work_dir=\"$work_dir\"; \
  export platform=\"$platform\"; \
  export platform_version=\"$platform_version\"; \
  export source=\"$source\"; \
  export BUILD_TAG=\"$BUILD_TAG\"; \
  "

if [ "$already_running" != "ok" ]
then
  echo "install packages on $image"
  ssh $sshopt "$remote_build_cmd ./MaxScale/BUILD/$install_script"
  installres=$?
  if [ $installres -ne 0 ]
  then
    exit $installres
  fi

  $HOME/mdbci/mdbci snapshot take --path-to-nodes $box --snapshot-name clean

else
  echo "already running VM, not installing deps"
fi

echo "run build on $box"
ssh $sshopt "$remote_build_cmd ./MaxScale/BUILD/$build_script"
if [ $? -ne 0 ] ; then
  echo "Error build on $box"
  exit 4
fi

echo "copying binaries to the '$pre_repo_dir/$target/$box'"
scp $scpopt $sshuser@$IP:$work_dir/$files $pre_repo_dir/$target/$box/
scp $scpopt $sshuser@$IP:$work_dir/$tars $pre_repo_dir/$target/$box/


echo "package building for '$target' for '$platform' '$platform_version' done!"



