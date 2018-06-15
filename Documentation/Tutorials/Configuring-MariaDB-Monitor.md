# Configuring the MariaDB Monitor

This document describes how to configure a MariaDB master-slave cluster monitor
to be used with MaxScale.

## Configuring the Monitor

Define the monitor that monitors the servers.

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

```sql
CREATE USER 'monitor_user'@'%' IDENTIFIED BY 'my_password';
GRANT REPLICATION CLIENT on *.* to 'monitor_user'@'%';
```

**Note:** If the automatic failover of the MariaDB Monitor will used, the user
will require additional grants. Execute the following SQL to grant them.
```sql
GRANT SUPER on *.* to 'monitor_user'@'%';
```
