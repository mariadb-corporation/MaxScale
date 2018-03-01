set -x
echo $*
export config_name="$1"
if [ -z $1 ] ; then
	config_name="local1"
fi

export curr_dir=`pwd`

export new_dirs="yes"

export maxscale_binlog_dir="/var/lib/maxscale/Binlog_Service"
export maxdir="/usr/bin/"
export maxdir_bin="/usr/bin/"
export maxscale_cnf="/etc/maxscale.cnf"
export maxscale_log_dir="/var/log/maxscale/"
export maxscale_sshkey=$maxscale_keyfile

cd $mdbci_dir

# Number of nodes
export node_N=4

export maxscale_IP=127.0.0.1
export maxscale_network=127.0.0.1
export maxscale_keyfile=$HOME/.ssh/id_rsa

# User name and Password for Master/Slave replication setup (should have all PRIVILEGES)
export node_user="skysql"
export node_password="skysql"

# User name and Password for Galera setup (should have all PRIVILEGES)
#export galera_user="skysql"
#export galera_password="skysql"

export maxscale_user="skysql"
export maxscale_password="skysql"

export maxadmin_password="mariadb"

#for prefix in "node" "galera"
for prefix in "node"
do
	N_var="$prefix"_N
	Nx=${!N_var}
	N=`expr $Nx - 1`
	for i in $(seq 0 $N)
	do
		num=`printf "%03d" $i`
                username=`whoami`
                eval 'export "$prefix"_"$num"_network=127.0.0.1'
                eval 'export "$prefix"_"$num"_private_ip=127.0.0.1'
                eval 'export "$prefix"_"$num"_hostname="$prefix""$num"'
                eval 'export "$prefix"_"$num"_whoami="$username"'
                eval 'export "$prefix"_"$num"_keyfile="$HOME"/.ssh/id_rsa'
                j=`expr $i + 1`
                eval 'export "$prefix"_"$num"_socket=/var/run/mysqld/mysqld"$j".sock'

                mariadbport=`expr $i + 3301`
		eval 'export "$prefix"_"$num"_port="$mariadbport"'
		eval 'export "$prefix"_"$num"_access_sudo=sudo'

		start_cmd_var="$prefix"_"$num"_start_db_command
		stop_cmd_var="$prefix"_"$num"_stop_db_command
                GRN=`expr $i + 1`
                eval 'export $start_cmd_var="mysqld_multi --defaults-file=$HOME/maxscale-system-test/local_tests/multiple_servers.cnf start $GRN"'
                eval 'export $stop_cmd_var="mysqld_multi --defaults-file=$HOME/maxscale-system-test/local_tests/multiple_servers.cnf  stop  $GRN"'

                start_cmd_var="$prefix"_"$num"_cleanup_db_command
                GRN=`expr $i + 1`
                eval 'export $start_cmd_var="$HOME/maxscale-system-test/local_tests/cleanup_db.sh $GRN"'

#		cd ..
	done
done

cd $mdbci_dir
export maxscale_access_user=`whoami`
export maxscale_whoami=`whoami`
export maxscale_access_sudo="sudo "

# Sysbench directory (should be sysbench >= 0.5)
export sysbench_dir="$HOME/sysbench_deb7/sysbench/"

export ssl=true

#export use_snapshots=yes
export take_snapshot_command="echo Snapshots are not supported in the local config"
export revert_snapshot_command="echo Snapshots are not supported in the local config"

export smoke=yes
cd $curr_dir
set +x
