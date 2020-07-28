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

