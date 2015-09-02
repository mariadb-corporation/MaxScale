#!/bin/bash

set -x

if [ -z $test_dir ] ; then
	test_dir=$maxdir/system-test/
fi

echo "Test $test_name"

test_name_space="$test_name "

template_line=`cat $test_dir/templates | grep $test_name_space`
a=( $template_line )
template=${a[1]}

if [ -z $template ] ; then
  template="replication"
fi

if [ -z $threads ] ; then
	threads=8
fi

cp $test_dir/cnf/maxscale.cnf.template.$template MaxScale.cnf
if [ $? -ne 0 ] ; then
	echo "error copying maxscale.cnf file"
	exit 1
fi

sed -i "s/###threads###/$threads/"  MaxScale.cnf

if [ "x$mysql51_only" == "xyes" ] ; then
	sed -i "s/module=mysqlmon/module=mysqlmon\nmysql51_replication=true/"  MaxScale.cnf
fi

for prefix in "repl" "galera"
do
	N_var="$prefix"_N
	Nx=${!N_var}
	N=`expr $Nx - 1`
	for i in $(seq 0 $N)
	do
		ip=`expr $IP_end + $i`
		num=`printf "%03d" $i`
		ip_var="$prefix"_"$num"
		ip=${!ip_var}
		server_num=`expr $i + 1`
		IP_str="s/###$prefix"
		IP_str+="_server_IP_$server_num###/$ip/"
		sed -i "$IP_str"  MaxScale.cnf

        	port_var="$prefix"_port_"$num"
	        port=${!port_var}
        	server_num=`expr $i + 1`
		port_str="s/###$prefix"
		port_str+="_server_port_$server_num###/$port/"
	        sed -i "$port_str"  MaxScale.cnf
	done
	Password_var="$prefix"_password
	User_var="$prefix"_user
	h_var="$prefix"_000
	port_var="$prefix"_port_000
	echo "CREATE DATABASE IF NOT EXISTS test" | mysql -p${!Password_var} -u${!User_var} -h ${!h_var}  -P ${!port_var}
done

if [ $access_user == "root" ] ; then
	export access_homedir="/root/"
else
	export access_homedir="/home/$access_user/"
fi

sed -i "s/###access_user###/$access_user/g" MaxScale.cnf
sed -i "s|###access_homedir###|$access_homedir|g" MaxScale.cnf

echo "copying maxscale.cnf using ssh $access_user"
scp -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null MaxScale.cnf $access_user@$maxscale_IP:./
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo cp MaxScale.cnf $maxscale_cnf"

scp -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $test_dir/ssl-cert/* $access_user@$maxscale_IP:./

ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo rm -rf $access_homedir/certs"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo mkdir $access_homedir/certs"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo cp *.pem $access_homedir/certs/"
cp $test_dir/ssl-cert/* .
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo chown maxscale:maxscale $access_homedir/certs -R"
#ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo chmod 664 $access_homedir/*.pem"
#ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo chmod a+r $access_homedir"
#ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo $maxdir_bin/maxkeys $max_dir/etc/.secrets"
if [ -z "$maxscale_restart" ] ; then
	export maxscale_restart="yes"
fi

if [ "$maxscale_restart" != "no" ] ; then
	echo "restarting Maxscale"
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo service maxscale stop"
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo killall -9 maxscale"
#	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo mkdir -p $access_homedir; $access_sudo chmod 777 -R $access_homedir"
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo  truncate -s 0 $maxscale_log_dir/error1.log ; $access_sudo chown maxscale:maxscale $maxscale_log_dir/error1.log; $access_sudo rm /tmp/core*; $access_sudo rm -rf /dev/shm/*;  $access_sudo ulimit -c unlimited; $access_sudo service maxscale restart" 
else
	echo "reloading Maxscale configuration"
#        ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "mkdir -p $access_homedir; chmod 777 -R $access_homedir"
	ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo service maxscale status | grep running"
	if [ $? == 0 ] ; then
#		ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo echo " " > $maxscale_log_dir/error1.log"
#                ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo echo " " > $maxscale_log_dir/messages1.log"
#                ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo echo " " > $maxscale_log_dir/debug1.log"
#                ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo echo " " > $maxscale_log_dir/trace1.log"

	        ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo rm /tmp/core*; $access_sudo truncate -s 0 $maxscale_log_dir/error1.log ; $access_sudo chown maxscale:maxscale $maxscale_log_dir/error1.log; $access_sudo ulimit -c unlimited; $access_sudo killall -HUP maxscale" 
	else
		ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $access_user@$maxscale_IP "$access_sudo rm /tmp/core*; $access_sudo truncate -s 0 $maxscale_log_dir/error1.log ; $access_sudo chown maxscale:maxscale $maxscale_log_dir/error1.log; $access_sudo ulimit -c unlimited; $access_sudo service maxscale start"
	fi
fi
#sleep 15
#disown
