# MySQL Monitor

## Overview

The MySQL Monitor is a monitoring module for MaxScale that monitors a Master-Slave replication cluster. It assigns master and slave roles inside MaxScale according to the actual replication tree in the cluster.

## Configuration

A minimal configuration for a  monitor requires a set of servers for monitoring and a username and a password to connect to these servers.

```
[MySQL Monitor]
type=monitor
module=mysqlmon
servers=server1,server2,server3
user=myuser
passwd=mypwd

```

The user requires the REPLICATION CLIENT privilege to successfully monitor the state of the servers.

```
MariaDB [(none)]> grant replication client on *.* to 'maxscale'@'maxscalehost';
Query OK, 0 rows affected (0.00 sec)
```

## Optional parameters for all monitors

Here are optional parameters that are common for all the monitors.

### `monitor_interval`

This is the time the monitor waits between each cycle of monitoring. The default value of 10000 milliseconds (10 seconds) should be lowered if you want a faster response to changes in the server states. The value is defined in milliseconds and the smallest possible value is 100 milliseconds.

```
monitor_interval=2500
```

### `backend_connect_timeout`

This parameter controls the timeout for connecting to a monitored server. It is in seconds and the minimum value is 1 second. The default value for this parameter is 3 seconds.

```
backend_connect_timeout=6
```

### `backend_write_timeout`

This parameter controls the timeout for writing to a monitored server. It is in seconds and the minimum value is 1 second. The default value for this parameter is 2 seconds.

```
backend_write_timeout=4
```

### `backend_read_timeout`

This parameter controls the timeout for reading from a monitored server. It is in seconds and the minimum value is 1 second. The default value for this parameter is 1 seconds.

```
backend_read_timeout=2
```

## MySQL Monitor optional parameters

These are optional parameters specific to the MySQL Monitor.

### `detect_replication_lag`

Detect replication lag between the master and the slaves. This allows the routers to route read queries to only slaves that are up to date.

```
detect_replication_lag=true
```

### `detect_stale_master`

Allow previous master to be available even in case of stopped or misconfigured 
replication. This allows services that depend on master and slave roles to continue functioning as long as the master server is available.

This is a situation which can happen if all slave servers are unreachable or the replication breaks for some reason.

```
detect_stale_master=true
```
 
### `script`

This script will be executed when a server changes its state. The parameter should be an absolute path to the script or it should be in the executable path. The user which is used to run MaxScale should have execution rights to the file itself and the directory it resides in.

```
script=/home/user/script.sh
```

This script will be called with the following command line arguments.

```
<name of the script> --event=<event type> --initiator=<server whose state changed> --nodelist=<list of all servers>
```
### `events`

A list of event names which cause the script to be executed. If this option is not defined, all events cause the script to be executed. The list must contain a comma separated list of event names.

```
events=master_down,slave_down
```

### `mysql51_replication`

Enable support for MySQL 5.1 replication monitoring. This is needed if a MySQL server older than 5.5 is used as a slave in replication.

```
mysql51_replication=true
```

## Script events

Here is a table of all possible event types and their descriptions.

Event Name|Description
----------|----------
master_down|A Master server has gone down
master_up|A Master server has come up
slave_down|A Slave server has gone down
slave_up|A Slave server has come up
server_down|A server with no assigned role has gone down
server_up|A server with no assigned role has come up
lost_master|A server lost Master status
lost_slave|A server lost Slave status
new_master|A new Master was detected
new_slave|A new Slave was detected


## Example 1 - Monitor script

Here is an example shell script which sends an email to an admin when a server goes down.

```
#!/usr/bin/env bash

#This script assumes that the local mail server is configured properly
#The second argument is the event type
event=${$2/.*=/}
server=${$3/.*=/}
message="A server has gone down at `date`."
echo $message|mail -s "The event was $event for server $server." admin@my.org

```

Here is a monitor configuration that only triggers the script when a master or a slave server goes down.

```
[Database Monitor]
type=monitor
module=mysqlmon
servers=server1,server2
script=mail_to_admin.sh
events=master_down,slave_down
```

When a master or a slave server goes down, the script is executed, a mail is sent and the administrator will be immediately notified of any possible problems.
This is just a simple example showing what you can do with MaxScale and monitor scripts.
