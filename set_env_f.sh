# $1 - Last digits of first machine IP
# $2 - galera IP_end

IP_end=$1
galeraIP=$2

if [ -z $galeraIP ] ; then
	galeraIP=`expr $IP_end + 5`
fi

r1=`expr $IP_end + 1`
r2=`expr $IP_end + 2`
r3=`expr $IP_end + 3`
r4=`expr $IP_end + 4`

g1=`expr 1 + $galeraIP`
g2=`expr 2 + $galeraIP`
g3=`expr 3 + $galeraIP`
g4=`expr 4 + $galeraIP`


export galera_N=4
export repl_N=4

export repl_000="192.168.122.$r1"
export repl_001="192.168.122.$r2"
export repl_002="192.168.122.$r3"
export repl_003="192.168.122.$r4"

export galera_000="192.168.122.$g1"
export galera_001="192.168.122.$g2"
export galera_002="192.168.122.$g3"
export galera_003="192.168.122.$g4"

export repl_port_000=3306
export repl_port_001=3306
export repl_port_002=3306
export repl_port_003=3306

export galera_port_000=3306
export galera_port_001=3306
export galera_port_002=3306
export galera_port_003=3306

export Maxscale_IP="192.168.122.$IP_end"

export repl_User="skysql"
export repl_Password="skysql"

export galera_User="skysql"
export galera_Password="skysql"

export KillVMCommand="/home/ec2-user/test-scripts/kill_vm.sh"
export StartVMCommand="/home/ec2-user/test-scripts/start_vm.sh"
export GetLogsCommand="/home/ec2-user/test-scripts/get_logs.sh"

export ImagesDir="/home/ec2-user/kvm/images"
export SSHKeysDir="/home/ec2-user/KEYS"
export TestVMsDir="/home/ec2-user/test-machines"


export repl_sshkey_000=$SSHKeysDir/`cat $TestVMsDir/image_name_$repl_000`
export repl_sshkey_001=$SSHKeysDir/`cat $TestVMsDir/image_name_$repl_001`
export repl_sshkey_002=$SSHKeysDir/`cat $TestVMsDir/image_name_$repl_002`
export repl_sshkey_003=$SSHKeysDir/`cat $TestVMsDir/image_name_$repl_003`

export galera_sshkey_000=$SSHKeysDir/`cat $TestVMsDir/image_name_$galera_000`
export galera_sshkey_001=$SSHKeysDir/`cat $TestVMsDir/image_name_$galera_001`
export galera_sshkey_002=$SSHKeysDir/`cat $TestVMsDir/image_name_$galera_002`
export galera_sshkey_003=$SSHKeysDir/`cat $TestVMsDir/image_name_$galera_003`

export Maxscale_sshkey=$SSHKeysDir/`cat $TestVMsDir/image_name_$Maxscale_IP`

export maxdir="/usr/local/skysql/maxscale/"
export SysbenchDir="/home/ec2-user/sysbench_deb7/sysbench/"
