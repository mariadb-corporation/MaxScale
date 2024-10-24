#!/bin/bash
#
# Copyright (c) 2016 MariaDB Corporation Ab
# Copyright (c) 2023 MariaDB plc, Finnish Branch
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2024-06-03
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

# $HOME/.config/mdbci/max-tst.key file should contain private key to access host

# read the name of build scripts directory
export script_dir="$(dirname $(readlink -f $0))"

rm -rf LOGS

export curr_date=`date '+%Y-%m-%d_%H-%M'`
export mdbci_config_name=${name:-$box-${curr_date}}
export mdbci_config_name=`echo ${mdbci_config_name} | sed "s/?//g"`
export MDBCI_VM_PATH=$HOME/${mdbci_config_name}_vms
export PATH=$PATH:$HOME/mdbci
export MDBCI_EXECUTABLE=`which mdbci`

. ${script_dir}/set_run_test_variables.sh

export name=$mdbci_config_name

# prepare separate dir for MDBCI vms
rm -rf $HOME/${mdbci_config_name}_vms
mkdir -p $HOME/${mdbci_config_name}_vms

export provider=`mdbci show provider $box --silent 2> /dev/null`
export backend_box=${backend_box:-"rocky_8_"$provider}

mdbci destroy --force test_vm

cp ${script_dir}/test_vm.json $HOME/${mdbci_config_name}_vms/

if [ "$provider" == "aws" ]
then
    test_vm_box=${test_vm_box:-"ubuntu_jammy_gcp"}
else
    test_vm_box=${test_vm_box:-"ubuntu_jammy_"$provider}
fi

if [ "$provider" == "ibm" ]
then
    test_vm_box=${test_vm_box:-"centos_9_ibm"}
    maria_product="mdbe"
else
    maria_product="mariadb"
fi

me=`whoami`
sed -i "s/###test_vm_box###/${test_vm_box}/"  $HOME/${mdbci_config_name}_vms/test_vm.json
sed -i "s/###test_vm_user###/${me}/" $HOME/${mdbci_config_name}_vms/test_vm.json

mdbci generate test_vm --template test_vm.json --override
mdbci up test_vm

mdbci install_product --product ${maria_product} --product-version 10.6 --include-unsupported test_vm/ubuntu_jammy
mdbci install_product --product plugin_mariadb_test --product-version 10.6 --include-unsupported test_vm/ubuntu_jammy
mdbci install_product --product plugin_gssapi_client --product-version 10.6 test_vm/ubuntu_jammy

ip=`mdbci show network --silent test_vm`
key=`mdbci show keyfile --silent test_vm`
sshopt="-o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o ConnectTimeout=120  "


ssh -i $key $sshopt $me@$ip "mkdir -p .ssh; mkdir -p ${MDBCI_VM_PATH}; mkdir -p mdbci; mkdir -p MaxScale"
rsync -e "ssh -i $key $sshopt" -a $(realpath ${script_dir}/../..)/ $me@$ip:/home/$me/MaxScale/
ssh -i $key $sshopt $me@$ip "chown -R $me:$me MaxScale"
ssh -i $key $sshopt $me@$ip "chmod -R a+r MaxScale"

scp -i $key $sshopt $HOME/.config/mdbci/max-tst.key $me@$ip:~/.ssh/id_rsa
ssh -i $key $sshopt $me@$ip "chmod 400 .ssh/id_rsa"
scp -i $key $sshopt ${script_dir}/mdbci_wrapper $me@$ip:~/mdbci/mdbci
ssh -i $key $sshopt $me@$ip "chmod +x mdbci/mdbci"

echo export MDBCI_VM_PATH=${MDBCI_VM_PATH} > test_env
echo export PATH=\$PATH:\$HOME/mdbci >> test_env
echo export host_user=$me  >> test_env
my_ip=`ip route get $ip | egrep -o '([0-9]{1,3}\.){3}[0-9]{1,3}' | tail -1`
echo export host_ip=${my_ip} >> test_env

test_env_list=(
    "WORKSPACE"
    "JOB_NAME"
    "BUILD_NUMBER"
    "BUILD_TIMESTAMP"
    "MDBCI_EXECUTABLE"
    "name"
    "target"
    "box"
    "backend_box"
    "product"
    "version"
    "do_not_destroy_vm"
    "test_set"
    "ci_url"
    "smoke"
    "big"
    "backend_ssl"
    "use_snapshots"
    "no_vm_revert"
    "template"
    "config_to_clone"
    "test_branch"
    "use_valgrind"
    "use_callgrind"
    "maxscale_product"
    "force_maxscale_version"
    "force_backend_version"
)

for s in ${test_env_list[@]} ; do
   eval "v=\$$s"
   if [ -n "$v" ] ; then
       echo "export $s=\"$v\"" >> test_env
   fi
done

cat test_env

scp -i $key $sshopt test_env $me@$ip:~/

ssh -i $key $sshopt $me@$ip "sudo usermod --shell /bin/bash $me"
ssh -i $key $sshopt $me@$ip "./MaxScale/BUILD/install_test_build_deps.sh"
ssh -i $key $sshopt $me@$ip ". ./test_env; env; ./MaxScale/system-test/mdbci/run_test.sh"
if [ $? != 0 ] ; then
    echo "Tests execution FAILED! exiting"
    exit 1
fi

. ${script_dir}/configure_log_dir.sh
mkdir -p LOGS
mkdir -p ${logs_publish_dir}
scp -i $key $sshopt -r $me@$ip:./MaxScale/build/system-test/LOGS/* LOGS/


cp core.* ${logs_publish_dir}
${script_dir}/copy_logs.sh

if [ "${do_not_destroy_vm}" != "yes" ] ; then
	mdbci destroy --force ${mdbci_config_name}
        mdbci destroy --force test_vm
        rm -rf $HOME/${mdbci_config_name}_vms
	echo "clean up done!"
fi
