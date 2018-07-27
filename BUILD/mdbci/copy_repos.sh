#!/bin/bash

# Copies repo from ${unsorted_repo_dir}/$target/$box to proper place in the public repo dir

dir=`pwd`
if [ "$box_type" == "RPM" ] ; then
        export arch=`ssh $sshopt "arch"`
        . ${script_dir}/generate_build_info_path.sh

        rm -rf $path_prefix/$platform/$platform_version/$arch/
        mkdir -p $path_prefix/$platform/$platform_version/$arch/
        rsync -avz  --progress ${unsorted_repo_dir}/$repo_name/$box/* $path_prefix/$platform/$platform_version/$arch/
	env > $build_info_path
        find $path_prefix/.. -type d -exec chmod 755 {} \;
        find $path_prefix/.. -type f -exec chmod 644 {} \;
        cd $path_prefix/$platform
        ln -s $platform_version "$platform_version"server
        ln -s $platform_version "$platform_version"Server

  eval "cat <<EOF
$(<${script_dir}/templates/repository-config/rpm.json.template)
" 2> /dev/null > ${path_prefix}/${platform}_${platform_version}.json


        echo "copying done"
else
        export arch=`ssh $sshopt "dpkg --print-architecture"`
        . ${script_dir}/generate_build_info_path.sh
        rm -rf $path_prefix/$platform_family/dists/$platform_version/main/binary-"$arch"
        rm -rf $path_prefix/$platform_family/dists/$platform_version/main/binary-i386
        mkdir -p $path_prefix/$platform_family/
        rsync -avz  --progress ${unsorted_repo_dir}/$repo_name/$box/* $path_prefix/$platform_family/
        env > $build_info_path
        find $path_prefix/.. -type d -exec chmod 755 {} \;
        find $path_prefix/.. -type f -exec chmod 644 {} \;
  eval "cat <<EOF
$(<${script_dir}/templates/repository-config/deb.json.template)
" 2> /dev/null > ${path_prefix}/${platform}_${platform_version}.json
fi
cd $dir

${mdbci_dir}/mdbci generate-product-repositories --product maxscale_ci --product-version $target
