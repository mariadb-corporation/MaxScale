# $1 - Last digits of first machine IP

IP_end=$1

r1=`expr $IP_end + 1`
r2=`expr $IP_end + 2`
r3=`expr $IP_end + 3`
r4=`expr $IP_end + 4`

g1=`expr $IP_end + 6`
g2=`expr $IP_end + 7`
g3=`expr $IP_end + 8`
g4=`expr $IP_end + 9`


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
