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

function stop_node() {
    do_ssh $1 "sudo /etc/init.d/mysql stop"
}

function start_node() {
    do_ssh $1 "sudo /etc/init.d/mysql start"
}

function do_test() {
    mysql -v -u skysql -pskysql -h $maxscale_IP -P $maxscale_port <<EOF
BEGIN;
INSERT INTO test.t1 (data) VALUES (now());
SELECT * FROM test.t1;
SELECT @@gtid_current_pos, @@server_id, @@hostname;
COMMIT;
EOF

    do_ssh maxscale "sudo maxadmin list servers"
}

# This is the port MaxScale is listening on
maxscale_port=4006

cat <<EOF

+--------------------------------+
| Preparing the test environment |
+--------------------------------+

EOF

# Configure replication-manager
do_ssh $mrm "sudo systemctl stop replication-manager"
do_ssh $mrm "sudo rm /etc/replication-manager/config.toml"
do_scp $mrm ~/system-test/mrm/config2.toml
do_ssh $mrm "sudo cp config2.toml /etc/replication-manager/config.toml"

# This configures and starts Maxscale
cd ~/system-test/
./check_backend
./non_native_setup replication_manager_3nodes
cd -

do_ssh $mrm "sudo replication-manager bootstrap --clean-all"
do_ssh $mrm "sudo systemctl start replication-manager"

cat <<EOF

+--------------------------------+
|   Test environment prepared    |
+--------------------------------+

EOF

###############################
# The actual demo starts here #
###############################

# echo "Press Enter to Start"
# read

# Create a table
mysql -v -u skysql -pskysql -h $maxscale_IP -P $maxscale_port <<EOF
CREATE OR REPLACE TABLE test.t1(id int auto_increment primary key, data datetime);
EOF

do_test

# Stop node-000 and node-001

# echo "Press Enter to Stop node-001"
# read

stop_node node_001
echo "Waiting 15 seconds"
sleep 15
do_test

# echo "Press Enter to Stop node-000"
# read

echo "inserting data"

for ((i=0;i<5000;i++))
do
    mysql -ss -u skysql -pskysql -h $maxscale_IP -P $maxscale_port -e "INSERT INTO test.t1 (data) VALUES (now());"
done

mysql -ss -u skysql -pskysql -h $maxscale_IP -P $maxscale_port -e "DELETE FROM test.t1 limit 5000"

echo "done"

stop_node node_000
echo "Waiting 15 seconds"
sleep 15
do_test

start_node node_000
echo "Waiting 15 seconds"
sleep 15
do_test

start_node node_001
echo "Waiting 15 seconds"
sleep 15
do_test

# Stop node-002

echo "Press Enter to Stop node-002"
read

stop_node node_002
echo "Waiting 15 seconds"
sleep 15
do_test

echo "Press Enter to Start node-002"
read

start_node node_002
echo "Waiting 15 seconds"
sleep 15
do_test

echo "Done!"
