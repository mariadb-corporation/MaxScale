#!/bin/bash

set -x

test_dir=$maxdir/system-test/

echo "Test $Test_name"

Test_name_space="$Test_name "

template_line=`cat $test_dir/templates | grep $Test_name_space`
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
	Passsword_var="$prefix"_Password
	User_var="$prefix"_User
	h_var="$prefix"_000
	port_var="$prefix"_port_000
	echo "CREATE DATABASE IF NOT EXISTS test" | mysql -p${!Password_var} -u${!User_var} -h ${!h_var}  -P ${!port_var}
done

scp -i $Maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null MaxScale.cnf root@$Maxscale_IP:$maxdir/etc/
#ssh -i $Maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$Maxscale_IP "$maxdir/bin/maxkeys $max_dir/etc/.secrets"
ssh -i $Maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$Maxscale_IP "rm $maxdir/log/*.log ; rm /tmp/core*; rm -rf /dev/shm/*; service maxscale restart" &
sleep 15
