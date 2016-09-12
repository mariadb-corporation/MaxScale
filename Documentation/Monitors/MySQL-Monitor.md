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

## Common Monitor Parameters

For a list of optional parameters that all monitors support, read the [Monitor Common](Monitor-Common.md) document.

## MySQL Monitor optional parameters

These are optional parameters specific to the MySQL Monitor.

### `detect_replication_lag`

A truth value which controls if replication lag between the master and the
slaves is monitored. This allows the routers to route read queries to only
slaves that are up to date. Default value for this parameter is false.

To detect the replication lag, MaxScale uses the _maxscale_schema.replication_heartbeat_
table. This table is created on the master server and it is updated at every heartbeat
with the current timestamp. The updates are then replicated to the slave servers
and when the replicated timestamp is read from the slave servers, the lag between
the slave and the master can be calculated.

The monitor user requires INSERT, UPDATE, DELETE and SELECT permissions on the
maxscale_schema.replication_heartbeat table and CREATE permissions on the
maxscale_schema database. The monitor user will always try to create the database
and the table if they do not exist.

### `detect_stale_master`

Allow previous master to be available even in case of stopped or misconfigured
replication.

Starting from MaxScale 2.0.0 this feature is enabled by default. It is disabled
by default in MaxScale 1.4.3 and below.

This allows services that depend on master and slave roles to continue
functioning as long as the master server is available. This is a situation
which can happen if all slave servers are unreachable or the replication
breaks for some reason.

```
detect_stale_master=true
```

### `detect_stale_slave`

Treat running slaves servers without a master server as valid slave servers.

This feature is enabled by default.

If a slave server loses its master server, the replication is considered broken.
With this parameter, slaves that have lost their master but have been slaves of
a master server can retain their slave status even without a master. This means
that when a slave loses its master, it can still be used for reads.

If this feature is disabled, a server is considered a valid slave if and only if
it has a running master server monitored by this monitor.

```
detect_stale_slave=true
```

### `mysql51_replication`

Enable support for MySQL 5.1 replication monitoring. This is needed if a MySQL server older than 5.5 is used as a slave in replication.

```
mysql51_replication=true
```

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
