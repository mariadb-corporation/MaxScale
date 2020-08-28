#!/bin/bash
set -x
export dir=`pwd`

# read the name of build scripts directory
export script_dir="$(dirname $(readlink -f $0))"

. ${script_dir}/set_run_test_variables.sh

if [ "$product" == "mysql" ] ; then
  export cnf_path=${script_dir}/cnf/mysql56
fi

mdbci destroy --force $name
mkdir -p ${MDBCI_VM_PATH}/$name

export cnf_path="${MDBCI_VM_PATH}/$name/cnf/"
if [ "$product" == "mysql" ] ; then
  export cnf_path=${MDBCI_VM_PATH}/$name/cnf/mysql56/
fi


  eval "cat <<EOF
$(<${script_dir}/templates/${template}.json.template)
" 2> /dev/null > ${MDBCI_VM_PATH}/${name}.json

mdbci --override --template  ${MDBCI_VM_PATH}/${name}.json generate $name

mkdir ${MDBCI_VM_PATH}/$name/cnf
cp -r ${script_dir}/cnf/* ${MDBCI_VM_PATH}/$name/cnf/

echo "running vagrant up $provider"

mdbci up $name --attempts 3 --labels MAXSCALE
if [ $? != 0 ]; then
	echo "Error creating configuration"
	exit 1
fi

#cp ~/build-scripts/team_keys .
mdbci public_keys --key ${team_keys} $name

exit 0
