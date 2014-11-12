#!/bin/bash

set -x

echo "Test $Test_name"

Test_name_space="$Test_name "

template_line=`cat /usr/local/skysql/maxscale/system-test/templates | grep $Test_name_space`
a=( $template_line )
template=${a[1]}


if [ -z $template ] ; then
  template="replication"
fi

echo $template | grep "galera"
if [ $? == 0 ] ; then
        prefix="galera"
	N=`expr $galera_N - 1`
else
        prefix="repl"
	N=`expr $repl_N - 1`
fi

if [ -z $threads ] ; then
	threads=8
fi


cp /usr/local/skysql/maxscale/system-test/cnf/maxscale.cnf.template.$template MaxScale.cnf
if [ $? -ne 0 ] ; then
	echo "error copying maxscale.cnf file"
	exit 1
fi

sed -i "s/###threads###/$threads/"  MaxScale.cnf

for i in $(seq 0 $N)
do
	ip=`expr $IP_end + $i`
	num=`printf "%03d" $i`
	ip_var="$prefix"_"$num"
	ip=${!ip_var}
	server_num=`expr $i + 1`
	sed -i "s/###server_IP_$server_num###/$ip/"  MaxScale.cnf

        port_var="$prefix"_port_"$num"
        port=${!port_var}
        server_num=`expr $i + 1`
        sed -i "s/###server_port_$server_num###/$port/"  MaxScale.cnf
done

echo "CREATE DATABASE IF NOT EXISTS test" | mysql -p$repl_Password -u$repl_User -h $repl_000 -P $repl_port_000


scp -i $Maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null MaxScale.cnf root@$Maxscale_IP:$maxdir/etc/
#ssh -i $Maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$Maxscale_IP "$maxdir/bin/maxkeys $max_dir/etc/.secrets"
ssh -i $Maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$Maxscale_IP "rm $maxdir/log/*.log ; rm /tmp/core*; service maxscale restart" &
sleep 15
