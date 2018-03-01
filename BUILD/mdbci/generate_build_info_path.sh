#! /bin/bash

set -x

export web_prefix=$(echo $path_prefix | sed "s|${repo_path}/||g")

if [ "$box_type" == "RPM" ] ; then
        export build_info_file="$platform/$platform_version/$arch/build_info"
else
        export build_info_file="$platform_family/dists/$platform_version/main/binary-$arch/build_info"
fi

echo "BUILD_PATH_INFO=$web_prefix/$build_info_file" > $dir/build_info_env_var_$BUILD_ID

export build_info_path=$path_prefix/$build_info_file
