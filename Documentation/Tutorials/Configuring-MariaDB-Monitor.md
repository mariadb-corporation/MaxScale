# Configuring the MariaDB Monitor

This document describes how to configure a MariaDB master-slave cluster monitor to be used with MaxScale.

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
[Replication-Monitor]
type=monitor
module=mariadbmon
servers=dbserv1, dbserv2, dbserv3
user=monitor_user
password=my_password
monitor_interval=2000
```

The mandatory parameters are the object type, the monitor module to use, the
list of servers to monitor and the username and password to use when connecting
to the servers. The `monitor_interval` parameter controls how many milliseconds
the monitor waits between each monitoring loop.

## Monitor User

The monitor user requires the REPLICATION CLIENT privileges to do basic
monitoring. To create a user with the proper grants, execute the following SQL.

```
CREATE USER 'monitor_user'@'%' IDENTIFIED BY 'my_password';
GRANT REPLICATION CLIENT on *.* to 'monitor_user'@'%';
```
