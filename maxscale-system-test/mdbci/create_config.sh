#!/bin/bash
set -x
export dir=`pwd`

# read the name of build scripts directory
export script_dir="$(dirname $(readlink -f $0))"

. ${script_dir}/set_run_test_variables.sh

${mdbci_dir}/repository-config/generate_all.sh repo.d
${mdbci_dir}/repository-config/maxscale-ci.sh $target repo.d


export repo_dir=$dir/repo.d/

export provider=`${mdbci_dir}/mdbci show provider $box --silent 2> /dev/null`
export backend_box=${backend_box:-"centos_7_"$provider}

mkdir -p ${MDBCI_VM_PATH}/$name
cd ${MDBCI_VM_PATH}/$name
vagrant destroy -f
cd $dir

export cnf_path="${MDBCI_VM_PATH}/$name/cnf/"
if [ "$product" == "mysql" ] ; then
  export cnf_path=${MDBCI_VM_PATH}/$name/cnf/mysql56/
fi


  eval "cat <<EOF
$(<${script_dir}/templates/${template}.json.template)
" 2> /dev/null > ${MDBCI_VM_PATH}/${name}.json

${mdbci_dir}/mdbci --override --template  ${MDBCI_VM_PATH}/${name}.json --repo-dir ${repo_dir} generate $name

mkdir ${MDBCI_VM_PATH}/$name/cnf
cp -r ${script_dir}/cnf/* ${MDBCI_VM_PATH}/$name/cnf/


while [ -f ~/vagrant_lock ]
do
	echo "vagrant is locked, waiting ..."
	sleep 5
done
touch ~/vagrant_lock
echo ${JOB_NAME}-${BUILD_NUMBER} >> ~/vagrant_lock

echo "running vagrant up $provider"

${mdbci_dir}/mdbci up $name --attempts 3
if [ $? != 0 ]; then
	echo "Error creating configuration"
	rm ~/vagrant_lock
	exit 1
fi

#cp ~/build-scripts/team_keys .
${mdbci_dir}/mdbci public_keys --key ${team_keys} $name

rm ~/vagrant_lock
exit 0
