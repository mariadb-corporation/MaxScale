set -x
rsync -a --no-o --no-g LOGS ${logs_publish_dir}
chmod a+r ${logs_publish_dir}/*
cp -r ${MDBCI_VM_PATH}/$name ${logs_publish_dir}
cp  ${MDBCI_VM_PATH}/${name}.json ${logs_publish_dir}
