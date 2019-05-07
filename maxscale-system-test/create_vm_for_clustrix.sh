#!/bin/bash

# Start 4 AWS instances and put info into _network_config
# $1 - path to MDBCI configuration 

sg=`cat ~/.config/mdbci/config.yaml | grep security_group | sed "s/security_group://" | tr -d " "`
i_ids=`aws ec2 run-instances --image ami-65fa5c16 --instance-type m3.large --security-groups turenko-home_1547635960 --key-name maxscale --count 4 | grep InstanceId | tr -d ' ' | tr -d ',' | tr -d '"' | sed "s/InstanceId://" | sed ':a;N;$!ba;s/\n/ /g'`
#i_ids=`aws ec2 run-instances --image ami-0ff760d16d9497662 --instance-type i3.large --security-groups turenko-home_1547635960 --key-name maxscale --count 4 | grep InstanceId | tr -d ' ' | tr -d ',' | tr -d '"' | sed "s/InstanceId://" | sed ':a;N;$!ba;s/\n/ /g'`
aws ec2 wait instance-running --instance-ids ${i_ids}
j=0
for i in ${i_ids}
do
    ip=`aws ec2 describe-instances --instance-id $i | grep "PublicIpAddress" | tr -d ' ' | tr -d ',' | tr -d '"' | sed "s/PublicIpAddress://"`
    pip=`aws ec2 describe-instances --instance-id $i | grep "PrivateIpAddress" | tr -d ' ' | tr -d ',' | tr -d '"' | sed "s/PrivateIpAddress://"`
    num=`printf "%03d" $j`
    j=`expr $j + 1`
    echo "clustrix_${num}_network=$ip" >> $1_network_config
    echo "clustrix_${num}_private=$pip" >> $1_network_config
    echo "clustrix_${num}_keyfile=~/.config/mdbci/maxscale.pem" >> $1_network_config
    echo "clustrix_${num}_hostname=clusterix${num}" >> $1_network_config
    echo "clustrix_${num}_whoami=ec2-user" >> $1_network_config
done

labels=`cat $1_configured_labels`
echo "$labels,CLUSTRIX_BACKEND" > $1_configured_labels
