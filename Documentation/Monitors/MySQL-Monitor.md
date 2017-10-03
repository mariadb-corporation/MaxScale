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

A boolean value which controls if replication lag between the master and the
slaves is monitored. This allows the routers to route read queries to only
slaves that are up to date. Default value for this parameter is _false_.

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

Enable support for MySQL 5.1 replication monitoring. This is needed if a MySQL
server older than 5.5 is used as a slave in replication.

```
mysql51_replication=true
```

### `multimaster`

Detect multi-master replication topologies. This feature is disabled by default.

When enabled, the multi-master detection looks for the root master servers in
the replication clusters. These masters can be found by detecting cycles in the
graph created by the servers. When a cycle is detected, it is assigned a master
group ID. Every master in a master group will receive the Master status. The
special group ID 0 is assigned to all servers which are not a part of a
multi-master replication cycle.

If one or more masters in a group has the `@@read_only` system variable set to
`ON`, those servers will receive the Slave status even though they are in the
multi-master group. Slave servers with `@@read_only` disabled will never receive
the master status.

By setting the servers into read-only mode, the user can control which
server receive the master status. To do this:

- Enable `@@read_only` on all servers (preferably through the configuration file)
- Manually disable `@@read_only` on the server which should be the master

This functionality is similar to the [Multi-Master Monitor](MM-Monitor.md)
functionality. The only difference is that the MySQL monitor will also detect
traditional Master-Slave topologies.

### `detect_standalone_master`

Detect standalone master servers. This feature takes a boolean parameter and is
disabled by default. In MaxScale 2.1.0, this parameter was called `failover`.

This parameter is intended to be used with simple, two node master-slave pairs
where the failure of the master can be resolved by "promoting" the slave as the
new master. Normally this is done by using an external agent of some sort
(possibly triggered by MaxScale's monitor scripts), like
[MariaDB Replication Manager](https://github.com/tanji/replication-manager)
or [MHA](https://code.google.com/p/mysql-master-ha/).

When the number of running servers in the cluster drops down to one, MaxScale
cannot be absolutely certain whether the last remaining server is a master or a
slave. At this point, MaxScale will try to deduce the type of the server by
looking at the system variables of the server in question.

By default, MaxScale will only attempt to deduce if the server can be used as a
slave server (controlled by the `detect_stale_slave` parameter). When the
`detect_standalone_master` mode is enabled, MaxScale will also attempt to deduce
whether the server can be used as a master server. This is done by checking that
the server is not in read-only mode and that it is not configured as a slave.

This mode in mysqlmon is completely passive in the sense that it does not modify
the cluster or any of the servers in it. It only labels the last remaining
server in a cluster as the master server.

Before a server is labelled as a standalone master, the following conditions must
have been met:

- Previous attempts to connect to other servers in the cluster have failed,
  controlled by the `failcount` parameter

- There is only one running server among the monitored servers

- The value of the `@@read_only` system variable is set to `OFF`

In 2.1.1, the following additional condition was added:

- The last running server is not configured as a slave

If the value of the `allow_cluster_recovery` parameter is set to false, the monitor
sets all other servers into maintenance mode. This is done to prevent accidental
use of the failed servers if they came back online. If the failed servers come
back up, the maintenance mode needs to be manually cleared once replication has
been set up.

**Note**: A failover will cause permanent changes in the data of the promoted
  server. Only use this feature if you know that the slave servers are capable
  of acting as master servers.

### `failcount`

Number of failures that must occur on all failed servers before a standalone
server is labelled as a master. The default value is 5 failures.

The monitor will attempt to contact all servers once per monitoring cycle. When
`detect_standalone_master` is enabled, all of the failed servers must fail
_failcount_ number of connection attempts before the last server is labeled as
the master.

The formula for calculating the actual number of milliseconds before the server
is labelled as the master is `monitor_interval * failcount`.

### `allow_cluster_recovery`

Allow recovery after the cluster has dropped down to one server. This feature
takes a boolean parameter is enabled by default. This parameter requires that
`detect_standalone_master` is set to true. In MaxScale 2.1.0, this parameter was
called `failover_recovery`.

When this parameter is disabled, if the last remaining server is labelled as the
master, the monitor will set all of the failed servers into maintenance
mode. When this option is enabled, the failed servers are allowed to rejoin the
cluster.

This option should be enabled only when MaxScale is used in conjunction with an
external agent that automatically reintegrates failed servers into the
cluster. One of these agents is the _replication-manager_ which automatically
configures the failed servers as new slaves of the current master.

### `allow_external_slaves`

Allow the use of external slaves. This option is enabled by default.

If a slave server is replicating from a master that is not being monitored by
the MySQL monitor, the slaves will be assigned the _Slave of External Server_
status (a status mainly for informational purposes).

When the `allow_external_slaves` option is enabled, the server will also be
assigned the _Slave_ status which allows them to be used like normal slave
servers. When the option is disabled, the servers will only receive the _Slave
of External Server_ status and they will not be used.

### `failover`

Enable automated master failover. This parameter expects a boolean value and the
default value is false.

When the failover functionality is enabled, traditional MariaDB Master-Slave
clusters will automatically elect a new master if the old master goes down. The
failover functionality will not take place when MaxScale is configured as a
passive instance. For details on how MaxScale behaves in passive mode, see the
following documentation of `failover_timeout`.

If an attempt at failover fails or multiple master servers are detected, an
error is logged and the failover functionality is disabled. If this happens, the
cluster must be fixed manually and the failover needs to be re-enabled via the
REST API or MaxAdmin.

### `failover_timeout`

The timeout for the cluster failover in seconds. The default value is 90
seconds.

If no successful failover takes place within the configured time period, a
message is logged and the failover functionality is disabled.

This parameter also controls how long a MaxScale instance that has transitioned
from passive to active will wait for a failover to take place after an apparent
loss of a master server. If no new master server is detected within the
configured time period, the failover will be initiated again.

### `switchover`

Enable switchover via MaxScale. This parameter expects a boolean value and
the default value is false.

When the switchover functionality is enabled, a REST API endpoint will be
made available, using which switchover may be performed. The endpoint will
be available irrespective of whether MaxScale is in active or passive mode,
but switchover will only be attempted if MaxScale is in active mode and an
error logged if an attempt is made when MaxScale is in passive mode.
Switchover may also be triggered from MaxAdmin and the same rules regarding
active/passive holds.

It is safe to perform switchover even with the failover functionality
enabled, as MaxScale will disable the failover behaviour for the duration
of the switchover.

Only if the switchover succeeds, will the failover functionality be re-enabled.
Otherwise it will remain disabled and must be turned on manually via the REST
API or MaxAdmin.

When switchover is iniated via the REST-API, the URL path looks as follows:
```
/v1/maxscale/mysqlmon/switchover?<monitor-instance>&<new-master>&<current-master>
```
where `<monitor-instance>` is the monitor section mame from the MaxScale
configuration file, `<new-master>` the name of the server that should be
made into the new master and `<current-master>` the server that currently
is the master. If there is no master currently, then `<current-master>`
need not be specified.

So, given a MaxScale configuration file like
```
[Cluster1]
type=monitor
module=mysqlmon
servers=server1, server2, server3, server 4
...
```
with the assumption that `server2` is the current master, then the URL
path for making `server4` the new master would be:
```
/v1/maxscale/mysqlmon/switchover?Cluster1&server4&server2
```

### `switchover_script`

*NOTE* By default, MariaDB MaxScale uses the MariaDB provided switchover
script, so `switchover_script` need not be specified.

This command will be executed when MaxScale has been told to perform a
switchover, either via MaxAdmin or the REST-API. The parameter should be an
absolute path to a command or the command should be in the executable path.
The user which is used to run MaxScale should have execution rights to the
file itself and the directory it resides in.

```
script=/home/user/myswitchover.sh current_master=$CURRENT_MASTER new_master=$NEW_MASTER
```

In addition to the substitutions documented in
[Common Monitor Parameters](./Monitor-Common.md)
the following substitutions will be made to the parameter value:

* `$CURRENT_MASTER` will be replaced with the IP and port of the current
  master. If the is no current master, the value will be `none`.
* `$NEW_MASTER` will be replaced with the IP and port of the server that
  should be made into the new master.

The script should return 0 for success and a non-zero value for failure.

### `switchover_timeout`

The timeout for the cluster switchover in seconds. The default value is 90
seconds.

If no successful switchover takes place within the configured time period,
a message is logged and the failover (not switchover) functionality will not
be enabled, even if it was enabled before the switchover attempt.

## Using the MySQL Monitor With Binlogrouter

Since MaxScale 2.2 it's possible to detect a replication setup
which includes Binlog Server: the required action is to add the
binlog server to the list of servers only if _master_id_ identity is set.

For addition information read the
[Replication Proxy](../Tutorials/Replication-Proxy-Binlog-Router-Tutorial.md)
tutorial.

## Example 1 - Monitor script

Here is an example shell script which sends an email to an admin@my.org
when a server goes down.

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

When a master or a slave server goes down, the script is executed, a mail is
sent and the administrator will be immediately notified of any possible
problems.  This is just a simple example showing what you can do with MaxScale
and monitor scripts.
