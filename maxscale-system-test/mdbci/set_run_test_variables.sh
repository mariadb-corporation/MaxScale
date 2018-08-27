#!/bin/bash


export MDBCI_VM_PATH=${MDBCI_VM_PATH:-$HOME/vms}
mkdir -p $MDBCI_VM_PATH
echo "MDBCI_VM_PATH=$MDBCI_VM_PATH"

export box=${box:-"centos_7_libvirt"}
echo "box=$box"

export template=${template:-"twomaxscales_full"}

export curr_date=`date '+%Y-%m-%d_%H-%M'`

export name=${name:-$box-${curr_date}}

export mdbci_dir=${mdbci_dir:-"$HOME/mdbci/"}
export ci_url=${ci_url:-"http://max-tst-01.mariadb.com/ci-repository/"}

export product=${product:-"mariadb"}
export version=${version:-"10.2"}
export target=${target:-"develop"}
export vm_memory=${vm_memory:-"2048"}
export JOB_NAME=${JOB_NAME:-"local_test"}
export BUILD_NUMBER=${BUILD_NUMBER:-`date '+%Y%m%d%H%M'`}
export BUILD_TAG=${BUILD_TAG:-jenkins-${JOB_NAME}-${BUILD_NUMBER}}
export team_keys=${team_keys:-${HOME}/team_keys}
export galera_version=${galera_version:-$version}
export do_not_destroy_vm=${do_not_destroy_vm:-"yes"}
#export test_set=${test_set:-"-LE UNSTABLE"}
export test_set=${test_set:-"-I 1,5"}
