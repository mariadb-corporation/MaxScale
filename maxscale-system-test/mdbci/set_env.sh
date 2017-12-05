#!/bin/bash
set -x
echo $*
export MDBCI_VM_PATH=${MDBCI_VM_PATH:-$HOME/vms}
export mdbci_dir=${mdbci_dir:-"$HOME/mdbci/"}

export config_name="$1"
if [ -z $1 ] ; then
	config_name="test1"
fi

export curr_dir=`pwd`

export maxscale_binlog_dir="/var/lib/maxscale/Binlog_Service"
export maxdir="/usr/bin/"
export maxdir_bin="/usr/bin/"
export maxscale_cnf="/etc/maxscale.cnf"
export maxscale_log_dir="/var/log/maxscale/"

# Number of nodes
export galera_N=`cat "$MDBCI_VM_PATH/$config_name"_network_config | grep galera | grep network | wc -l`
export node_N=`cat "$MDBCI_VM_PATH/$config_name"_network_config | grep node | grep network | wc -l`
sed "s/^/export /g" "$MDBCI_VM_PATH/$config_name"_network_config > "$curr_dir"/"$config_name"_network_config_export
source "$curr_dir"/"$config_name"_network_config_export

# IP Of MaxScale machine
export maxscale_IP=$maxscale_network
export maxscale_sshkey=$maxscale_keyfile

# User name and Password for Master/Slave replication setup (should have all PRIVILEGES)
export node_user="skysql"
export node_password="skysql"

# User name and Password for Galera setup (should have all PRIVILEGES)
export galera_user="skysql"
export galera_password="skysql"

export maxscale_user="skysql"
export maxscale_password="skysql"

export maxadmin_password="mariadb"

for prefix in "node" "galera"
do
	N_var="$prefix"_N
	Nx=${!N_var}
	N=`expr $Nx - 1`
	for i in $(seq 0 $N)
	do
		num=`printf "%03d" $i`
		eval 'export "$prefix"_"$num"_port=3306'
		eval 'export "$prefix"_"$num"_access_sudo=sudo'

		start_cmd_var="$prefix"_"$num"_start_db_command
		stop_cmd_var="$prefix"_"$num"_stop_db_command
		mysql_exe=`${mdbci_dir}/mdbci ssh --command 'ls /etc/init.d/mysql* 2> /dev/null | tr -cd "[:print:]"' $config_name/node_$num  --silent 2> /dev/null`
		echo $mysql_exe | grep -i "mysql"
		if [ $? != 0 ] ; then
			service_name=`${mdbci_dir}/mdbci ssh --command 'systemctl list-unit-files | grep mysql' $config_name/node_$num  --silent`
			echo $service_name | grep mysql
			if [ $? == 0 ] ; then
				echo $service_name | grep mysqld
				if [ $? == 0 ] ; then
		                        eval 'export $start_cmd_var="service mysqld start "'
	        	                eval 'export $stop_cmd_var="service mysqld stop  "'
				else
	                        	eval 'export $start_cmd_var="service mysql start "'
	        	                eval 'export $stop_cmd_var="service mysql stop  "'
				fi
			else
				${mdbci_dir}/mdbci ssh --command 'echo \"/usr/sbin/mysqld \$* 2> stderr.log > stdout.log &\" > mysql_start.sh; echo \"sleep 20\" >> mysql_start.sh; echo \"disown\" >> mysql_start.sh; chmod a+x mysql_start.sh' $config_name/node_$num --silent
        	                eval 'export $start_cmd_var="/home/$au/mysql_start.sh "'
				eval 'export $start_cmd_var="killall mysqld "'
			fi
		else
			eval 'export $start_cmd_var="$mysql_exe start "'
			eval 'export $stop_cmd_var="$mysql_exe stop "'
		fi

		eval 'export "$prefix"_"$num"_start_vm_command="cd $mdbci_dir/$config_name;vagrant up node_$num --provider=$provider; cd $curr_dir"'
		eval 'export "$prefix"_"$num"_kill_vm_command="cd $mdbci_dir/$config_name;vagrant halt node_$num --provider=$provider; cd $curr_dir"'
	done
done

export maxscale_access_user=$maxscale_whoami
export maxscale_access_sudo="sudo "

# Sysbench directory (should be sysbench >= 0.5)
export sysbench_dir=${sysbench_dir:-"$HOME/sysbench_deb7/sysbench/"}

export ssl=true

export take_snapshot_command="${script_dir}/take_snapshot.sh $config_name"
export revert_snapshot_command="${script_dir}/revert_snapshot.sh $config_name"
#export use_snapshots=yes

set +x
