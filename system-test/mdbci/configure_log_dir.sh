#
# Copyright (c) 2016 MariaDB Corporation Ab
# Copyright (c) 2023 MariaDB plc, Finnish Branch
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2027-11-30
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

set -x
LOGS_DIR=${logs_dir:-$HOME/LOGS}
echo $JOB_NAME | grep "/"
if [ $? == 0 ] ; then
        export job_name_buildID=`echo $JOB_NAME | sed "s|/|-$BUILD_NUMBER/|"`
        export logs_publish_dir="${LOGS_DIR}/${job_name_buildID}/"
else
        export logs_publish_dir="${LOGS_DIR}/${JOB_NAME}-${BUILD_NUMBER}"
fi

export job_name_buildID=`echo ${JOB_NAME} | sed "s|/|-${BUILD_NUMBER}/|"`
export logs_publish_dir="${LOGS_DIR}/${job_name_buildID}-${BUILD_NUMBER}"
echo "Logs go to ${logs_publish_dir}"
mkdir -p ${logs_publish_dir}

