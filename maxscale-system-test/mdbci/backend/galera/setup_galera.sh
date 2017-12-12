#!/bin/bash

N=$galera_N

x=`expr $N - 1`
for i in $(seq 0 $x)
do
	num=`printf "%03d" $i`
	sshkey_var=galera_"$num"_keyfile
	user_var=galera_"$num"_whoami
	IP_var=galera_"$num"_network

	sshkey=${!sshkey_var}
	user=${!user_var}
	IP=${!IP_var}

    ssh -i $sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $user@$IP "sudo mysql_install_db; sudo chown -R mysql:mysql /var/lib/mysql"
done
