# Configuring the Galera Monitor

This document describes how to configure a Galera cluster monitor.

## Configuring the Servers

The first step is to define the servers that make up the cluster. These servers will be used by the services and are monitored by the monitor.

```
[dbserv1]
type=server
address=192.168.2.1
port=3306
protocol=MariaDBBackend

[dbserv2]
type=server
address=192.168.2.2
port=3306
protocol=MariaDBBackend

[dbserv3]
type=server
address=192.168.2.3
port=3306
protocol=MariaDBBackend
```

## Configuring the Monitor

The next step is to define the monitor that monitors the servers.

```
[Galera-Monitor]
type=monitor
module=galeramon
servers=dbserv1, dbserv2, dbserv3
user=monitor_user
password=my_password
monitor_interval=2000
```

The mandatory parameters are the object type, the monitor module to use, the
list of servers to monitor and the username and password to use when connecting
to the servers. The `monitor_interval` parameter controls how many milliseconds
the monitor waits between each monitoring loop.

This monitor module will assign one node within the Galera Cluster as the
current master and other nodes as slave. Only those nodes that are active
members of the cluster are considered when making the choice of master node. The
master node will be the node with the lowest value of `wsrep_local_index`.

## Monitor User

The monitor user does not require any special grants to monitor a Galera
cluster. To create a user for the monitor, execute the following SQL.

```
CREATE USER 'monitor_user'@'%' IDENTIFIED BY 'my_password';
```
