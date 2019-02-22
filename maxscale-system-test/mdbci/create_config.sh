#!/bin/bash
set -x
export dir=`pwd`

# read the name of build scripts directory
export script_dir="$(dirname $(readlink -f $0))"

. ${script_dir}/set_run_test_variables.sh

export provider=`${mdbci_dir}/mdbci show provider $box --silent 2> /dev/null`
export backend_box=${backend_box:-"centos_7_"$provider}

if [ "$product" == "mysql" ] ; then
  export cnf_path=${script_dir}/cnf/mysql56
fi

${mdbci_dir}/mdbci destroy $name
mkdir -p ${MDBCI_VM_PATH}/$name

export cnf_path="${MDBCI_VM_PATH}/$name/cnf/"
if [ "$product" == "mysql" ] ; then
  export cnf_path=${MDBCI_VM_PATH}/$name/cnf/mysql56/
fi


  eval "cat <<EOF
$(<${script_dir}/templates/${template}.json.template)
" 2> /dev/null > ${MDBCI_VM_PATH}/${name}.json

${mdbci_dir}/mdbci --override --template  ${MDBCI_VM_PATH}/${name}.json generate $name

mkdir ${MDBCI_VM_PATH}/$name/cnf
cp -r ${script_dir}/cnf/* ${MDBCI_VM_PATH}/$name/cnf/

echo "running vagrant up $provider"

${mdbci_dir}/mdbci up $name --attempts 3
if [ $? != 0 ]; then
	echo "Error creating configuration"
	rm -f ~/vagrant_lock
	exit 1
fi

#cp ~/build-scripts/team_keys .
${mdbci_dir}/mdbci public_keys --key ${team_keys} $name

rm -f ~/vagrant_lock
exit 0
