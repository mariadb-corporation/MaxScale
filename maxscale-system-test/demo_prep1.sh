#!/bin/bash

# The following environment variables must be set:
#
# maxscale_sshkey       The ssh key to the MaxScale VM
# maxscale_access_user  The username on the VM
# maxscale_IP           The IP address of the MaxScale VM
# node_000_network      IP address of the
#
# The Vagrant setup is located in ~/mdbci/my-test-build/. Vagrant is used
# for SSH access to the machines.
#
# The backend server also must have log-slave-updates enabled.

# This is the VM name where the replication-manager is installed
mrm=galera_000

# Helper functions for ssh and scp
function do_ssh() {
    cd ~/mdbci/my-test-build/
    vagrant ssh $1 -c "$2"
    cd - > /dev/null
}

# Helper functions for ssh and scp
function do_scp() {
    cd ~/mdbci/my-test-build/
    local dest=$(vagrant ssh-config $1|grep HostName|sed 's/.*HostName //')
    scp -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  $2 vagrant@$dest:~
    cd - > /dev/null
}

cat <<EOF

+--------------------------------+
| Preparing the test environment |
+--------------------------------+

EOF

# Configure replication-manager
do_ssh $mrm "sudo systemctl stop replication-manager"
do_ssh $mrm "sudo rm /etc/replication-manager/config.toml"
do_scp $mrm ~/system-test/mrm/config1.toml
do_ssh $mrm "sudo cp config1.toml /etc/replication-manager/config.toml"

# This configures and starts Maxscale
cd ~/system-test/
./non_native_setup replication_manager_2nodes replication_manager_2nodes
cd -

do_ssh $mrm "sudo replication-manager bootstrap --clean-all"
do_ssh $mrm "sudo systemctl start replication-manager"

cat <<EOF

+--------------------------------+
|   Test environment prepared    |
+--------------------------------+

EOF
