#!/bin/bash

set -x

template_line=`cat /usr/local/skysql/maxscale/system-test/templates | grep $Test_name`
a=( $template_line )
template=${a[1]}


if [ -z $template ] ; then
  template="replication"
fi

echo $template | grep "galera"
if [ $? == 0 ] ; then
        prefix="galera"
	N=$galera_N
else
        prefix="repl"
	N=$repl_N
fi

if [ -z $threads ] ; then
	threads=1
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
	sed -i "s/###server_IP_$i###/$ip/"  MaxScale.cnf
done

max_dir="/usr/local/skysql/maxscale/"
scp -i $Maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null MaxScale.cnf root@$Maxscale_IP:$max_dir/etc/
ssh -i $Maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$Maxscale_IP "$max_dir/bin/maxkeys $max_dir/etc/.secrets"
ssh -i $Maxscale_sshkey -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$Maxscale_IP "rm $max_dir/log/*.log ; rm /tmp/core*; service maxscale restart" &
sleep 15
