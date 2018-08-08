#!/bin/bash

set -x

# read the name of build scripts directory
export script_dir="$(dirname $(readlink -f $0))"

# load all needed variables
. ${script_dir}/set_build_variables.sh

export maxadmin_command=${maxadmin_command:-"sudo maxadmin show services"}

export old_target=${old_target:-"2.1.9"}
export old_target=`echo $old_target | sed "s/?//g"`

provider=`${mdbci_dir}/mdbci show provider $box --silent 2> /dev/null`
name=$box-${JOB_NAME}-${BUILD_NUMBER}_upgradetest
name=`echo $name | sed "s|/|-|g"`

eval "cat <<EOF
$(<${script_dir}/templates/install.json.template)
" 2> /dev/null > $MDBCI_VM_PATH/${name}.json

while [ -f ~/vagrant_lock ]
do
	sleep 5
done
touch ~/vagrant_lock
echo $JOB_NAME-$BUILD_NUMBER >> ~/vagrant_lock

# destroying existing box
if [ -d "install_$box" ]; then
        ${mdbci_dir}/mdbci destroy $name
fi

# starting VM for build
${mdbci_dir}/mdbci --override --template $MDBCI_VM_PATH/$name.json generate $name
${mdbci_dir}/mdbci up $name --attempts=1
if [ $? != 0 ] ; then
        if [ $? != 0 ] ; then
		echo "Error starting VM"
		if [ "x$do_not_destroy_vm" != "xyes" ] ; then
                        ${mdbci_dir}/mdbci destroy $name
		fi
		rm ~/vagrant_lock
		exit 1
	fi
fi

rm ~/vagrant_lock

# get VM info
export sshuser=`${mdbci_dir}/mdbci ssh --command 'whoami' --silent $name/maxscale 2> /dev/null | tr -d '\r'`
export IP=`${mdbci_dir}/mdbci show network $name/maxscale --silent 2> /dev/null`
export sshkey=`${mdbci_dir}/mdbci show keyfile $name/maxscale --silent 2> /dev/null | sed 's/"//g'`
export scpopt="-i $sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o ConnectTimeout=120 "
export sshopt="$scpopt $sshuser@$IP"

old_version=`ssh $sshopt "maxscale --version" `

${mdbci_dir}/mdbci setup_repo --product maxscale_ci --product-version ${target} $name/maxscale
${mdbci_dir}/mdbci install_product --product maxscale_ci $name/maxscale

res=$?

new_version=`ssh $sshopt "maxscale --version" `

echo "old version: '${old_version}', new version: '${new_version}'"
if [ "${old_version}" == "${new_version}" ]; then
	echo "Upgrde was not done!"
	res=1
fi

export cnf_file=${cnf_file:-"maxscale.cnf.minimum"}

scp $scpopt ${script_dir}/cnf/$cnf_file $sshuser@$IP:~/

. ${script_dir}/configure_log_dir.sh

${mdbci_dir}/mdbci ssh --command 'sudo service --help' $name/maxscale
if [ $? == 0 ] ; then
	maxscale_start_cmd="sudo service maxscale start"
else
        ${mdbci_dir}/mdbci ssh --command 'echo \"/usr/bin/maxscale 2> /dev/null &\" > maxscale_start.sh; echo \"disown\" >> maxscale_start.sh; chmod a+x maxscale_start.sh' $name/maxscale --silent
	maxscale_start_cmd="sudo ./maxscale_start.sh 2> /dev/null &"
fi

ssh $sshopt "sudo cp $cnf_file /etc/maxscale.cnf"
ssh $sshopt "$maxscale_start_cmd" &
pid_to_kill=$!

for  i in {1..10}
do
    sleep 5
    ssh $sshopt $maxadmin_command
    maxadm_exit=$?
    if [ $maxadm_exit == 0 ] ; then
        break
    fi
done

if [ $maxadm_exit != 0 ] ; then
	echo "Maxadmin executing error"
	res=1
fi

maxadmin_out=`ssh $sshopt $maxadmin_command`
echo $maxadmin_out | grep "CLI"
if [ $? != 0 ] ; then
	echo "CLI service is not found in maxadmin output"
        res=1
fi
echo $maxadmin_out | grep "Started"
if [ $? != 0 ] ; then
	echo "'Started' is not found in the CLI service description"
        res=1
fi

mkdir -p $logs_publish_dir
scp $scpopt $sshuser@$IP:/var/log/maxscale/* $logs_publish_dir
chmod a+r $logs_publish_dir/*

if [ "x$do_not_destroy_vm" != "xyes" ] ; then
        ${mdbci_dir}/mdbci destroy $name
fi
kill $pid_to_kill
exit $res
