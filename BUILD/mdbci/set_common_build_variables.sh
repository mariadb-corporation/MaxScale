#!/bin/bash

# Defines defaults values for all uninitialized environmental variables
# In case of running from Jenkins all values go from Jenkins parameters

export dir=`pwd`

export MDBCI_VM_PATH=${MDBCI_VM_PATH:-$HOME/vms}
mkdir -p $MDBCI_VM_PATH
echo "MDBCI_VM_PATH=$MDBCI_VM_PATH"

export box=${box:-"centos_7_libvirt"}
echo "box=$box"

# get commit ID
commitID=`git log | head -1 | sed "s/commit //"`
echo "commitID $commitID"

export branch=`git symbolic-ref --short HEAD`
export curr_date=`date '+%Y-%m-%d_%H-%M'`

export source=${source:-"$branch"}
echo "source=$source"

#hack to get rid of Jenkins artifacts 
export target=`echo $target | tr -cd "[:print:]" | sed "s/?//g" | sed "s/ //g"`

export target=${target:-"$source-$curr_date"}
echo "target=$target"

export product_name=${product_name:-"maxscale"}

export box_type="RPM"
echo $box | grep -i ubuntu
if [ $? == 0 ] ; then
  export box_type="DEB"
  export platform_family="ubuntu"
fi
echo $box | grep -i deb
if [ $? == 0 ] ; then
  export box_type="DEB"
  export platform_family="debian"
fi

export do_not_destroy_vm=${do_not_destroy_vm:-"no"}

export try_already_running=${try_already_running:-"no"}

export mdbci_dir=${mdbci_dir:-"$HOME/mdbci/"}

export repo_name=$target

export repo_path=${repo_path:-$HOME/repository}

export path_prefix="$repo_path/$repo_name/mariadb-$product_name/"

export ci_url=${ci_url:-"http://max-tst-01.mariadb.com/ci-repository/"}

export deb_repo_key=${deb_repo_key:-"135659e928c12247"}

export rpm_repo_key=${rpm_repo_key:-"$ci_url/MariaDBMaxscale-GPG-KEY.public"}
