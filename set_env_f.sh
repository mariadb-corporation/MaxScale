# $1 - Last digits of first machine IP

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

export Maxscale_IP="192.168.122.$IP_end"

export KillVMCommand="/home/ec2-user/test-scripts/kill_vm.sh"
export GetLogsCommand="/home/ec2-user/test-scripts/get_logs.sh"
