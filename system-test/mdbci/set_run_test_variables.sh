#!/bin/bash

export MDBCI_VM_PATH=${MDBCI_VM_PATH:-"$HOME/vms/"}
export curr_date=`date '+%Y-%m-%d_%H-%M'`
export mdbci_config_name=${name:-$box-${curr_date}}

export PATH=$PATH:$HOME/mdbci/

export JOB_NAME=${JOB_NAME:-"local_test"}
export BUILD_NUMBER=${BUILD_NUMBER:-`date '+%Y%m%d%H%M'`}
export BUILD_TAG=${BUILD_TAG:-jenkins-${JOB_NAME}-${BUILD_NUMBER}}
export team_keys=${team_keys:-${HOME}/team_keys}
export do_not_destroy_vm=${do_not_destroy_vm:-"yes"}
#export test_set=${test_set:-"-LE UNSTABLE"}
export test_set=${test_set:-"-I 1,5"}
export force_maxscale_version=${test_set:-"true"}
export force_backend_version=${test_set:-"false"}
