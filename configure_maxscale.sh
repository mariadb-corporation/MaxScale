#!/bin/bash

#set -x

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

scp -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null MaxScale.cnf root@$maxscale_IP:$maxscale_cnf
#ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "$maxdir/bin/maxkeys $max_dir/etc/.secrets"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "service maxscale stop"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "killall -9 maxscale"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "mkdir -p /home/ec2-user; chmod 777 -R /home/ec2-user"
ssh -i $maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$maxscale_IP "rm $maxscale_log_dir/*.log ; rm /tmp/core*; rm -rf /dev/shm/*;  ulimit -c unlimited; service maxscale restart" 
#sleep 15
#disown
