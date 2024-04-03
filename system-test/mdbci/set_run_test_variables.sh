#!/bin/bash
#
# Copyright (c) 2016 MariaDB Corporation Ab
# Copyright (c) 2023 MariaDB plc, Finnish Branch
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2028-04-03
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

export MDBCI_VM_PATH=${MDBCI_VM_PATH:-"$HOME/vms/"}
export curr_date=`date '+%Y-%m-%d_%H-%M'`
export mdbci_config_name=${name:-$box-${curr_date}}

export PATH=$PATH:$HOME/mdbci/

export JOB_NAME=${JOB_NAME:-"local_test"}
export BUILD_NUMBER=${BUILD_NUMBER:-`date '+%Y%m%d%H%M'`}
export BUILD_TAG=${BUILD_TAG:-jenkins-${JOB_NAME}-${BUILD_NUMBER}}
export team_keys=${team_keys:-${HOME}/team_keys}
export do_not_destroy_vm=${do_not_destroy_vm:-"yes"}
export test_set=${test_set:-"-I 1,5"}
export force_maxscale_version=${force_maxscale_version:-"true"}
export force_backend_version=${force_backend_version:-"false"}
