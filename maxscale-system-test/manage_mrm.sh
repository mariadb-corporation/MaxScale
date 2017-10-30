#!/bin/bash

function do_ssh() {
    ssh -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet $maxscale_access_user@$maxscale_IP
}

function do_scp() {
    scp -i $maxscale_sshkey -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=quiet $1 $maxscale_access_user@$maxscale_IP:$2
}

function create_config() {

    if [ "$1" == "3" ]
    then
        nodelist=$node_000_network:3306,$node_001_network:3306,$node_002_network:3306
    elif [ "$1" == "2" ]
    then
        nodelist=$node_000_network:3306,$node_001_network:3306
    else
        nodelist=$node_000_network:3306,$node_001_network:3306,$node_002_network:3306,$node_003_network:3306
    fi

    if [ -n "$new_replication_manager" ]
    then
        default_section="[Default]"
    fi

    cat <<EOF > config.toml
# config.toml
# Example replication-manager configuration file

$default_section
hosts = "$nodelist"
user = "skysql:skysql"
rpluser = "skysql:skysql"
title = "Cluster01"
connect-timeout = 1
prefmaster = "$node_000_network:3306"
interactive = false
log-level=1
# LOG
# ---

logfile = "/var/log/replication-manager.log"
verbose = true

# TOPOLOGY
# --------


# Automatically rejoin a failed server to the current master
# Slaves will re enter with read-only

readonly = true
failover-event-scheduler = false
failover-event-status = false

# FAILOVER
# --------

# Timeout in seconds between consecutive monitoring
# check type can be tcp or agent
monitoring-ticker = 1
check-type = "tcp"
check-replication-filters = true
check-binlog-filters = true
check-replication-state = true

# Failover after N failures detection
# Reset number of failure if server auto recover after N seconds
failcount = 1
failcount-reset-time = 300

# Cancel failover if already N failover
# Cancel failover if last failover was N seconds before
# Cancel failover in semi-sync when one slave is not in sync
# Cancel failover when replication delay is more than N seconds
failover-limit = 100
failover-time-limit = 1
failover-at-sync = false
switchover-at-sync = false
maxdelay = 30

# SWITCHOVER
# ----------

# In switchover Wait N milliseconds before killing long running transactions
# Cancel switchover if transaction running more than N seconds
# Cancel switchover if write query running more than N seconds
# Cancel switchover if one of the slaves is not synced based on GTID equality
wait-kill = 5000
wait-trx = 10
wait-write-query = 10
gtidcheck = true

EOF

}

function install_mrm() {

    # new_replication_manager means that we're using a custom build and it's already installed on the system
    if [ -z "$new_replication_manager" ]
    then
        do_ssh <<EOF
command -v wget > /dev/null || sudo yum -y install wget
wget -q https://github.com/tanji/replication-manager/releases/download/1.0.2/replication-manager-1.0.2_1_g8faf64d-8faf64d.x86_64.rpm
sudo yum -y install ./replication-manager-1.0.2_1_g8faf64d-8faf64d.x86_64.rpm
sudo systemctl daemon-reload
rm ./replication-manager-1.0.2_1_g8faf64d-8faf64d.x86_64.rpm
EOF
    fi

    create_config $1
    do_scp './config.toml' '~/config.toml'

    do_ssh <<EOF
sudo mkdir -p /etc/replication-manager/
sudo cp ./config.toml /etc/replication-manager/config.toml
EOF
}

function build_mrm() {
    do_ssh <<EOF
command -v wget > /dev/null || sudo yum -y install wget
test -f go1.8.linux-amd64.tar.gz || wget -q https://storage.googleapis.com/golang/go1.8.linux-amd64.tar.gz
sudo su -
cd /home/vagrant/
sudo tar -axf go1.8.linux-amd64.tar.gz -C /usr
sudo echo 'export GOROOT=/usr/go/' > /etc/profile.d/go.sh
sudo echo 'export GOPATH=/usr/' >> /etc/profile.d/go.sh
sudo echo 'export PATH=/$PATH:/usr/go/bin/' >> /etc/profile.d/go.sh
source /etc/profile
go get github.com/tanji/replication-manager
go install github.com/tanji/replication-manager
cp /usr/src/github.com/tanji/replication-manager/service/replication-manager.service /etc/systemd/system/
exit
EOF
}

function remove_mrm() {
        do_ssh <<EOF
sudo systemctl stop replication-manager
EOF
    if [ -z "$new_replication_manager" ]
    then
        do_ssh <<EOF
sudo yum -y remove replication-manager
sudo rm /etc/replication-manager/config.toml
sudo rm /var/log/replication-manager.log
EOF
    fi
}

case $1 in
    install)
        echo "`date` Installing replication-manager"
        install_mrm $2
        ;;

    configure)
        echo "`date` Creating replication-manager configuration"
        create_config $2
        ;;

    build)
        echo "`date` Building replication-manager from source"
        build_mrm
        ;;

    remove)
        echo "`date` Removing replication-manager"
        remove_mrm
        ;;

    *)
        echo "Usage: `basename $0` { install | remove }"
        ;;
esac
