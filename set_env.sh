export galera_N=4
export repl_N=4

export repl_000="192.168.122.106"
export repl_001="192.168.122.107"
export repl_002="192.168.122.108"
export repl_003="192.168.122.109"

export galera_000="192.168.122.111"
export galera_001="192.168.122.112"
export galera_002="192.168.122.113"
export galera_003="192.168.122.114"

export repl_port_000=3306
export repl_port_001=3306
export repl_port_002=3306
export repl_port_003=3306

export galera_port_000=3306
export galera_port_001=3306
export galera_port_002=3306
export galera_port_003=3306

export Maxscale_IP="192.168.122.105"

export KillVMCommand="/home/ec2-user/test-scripts/kill_vm.sh"
export StartVMCommand="/home/ec2-user/test-scripts/start_vm.sh"
export GetLogsCommand="/home/ec2-user/test-scripts/get_logs.sh"


repl_User="skysql"
repl_Password="skysql"

galera_User="skysql"
galera_Password="skysql"

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
