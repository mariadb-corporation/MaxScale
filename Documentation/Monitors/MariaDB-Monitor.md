# MariaDB Monitor

Up until MariaDB MaxScale 2.2.0, this monitor was called _MySQL Monitor_.

## Overview

The MariaDB Monitor is a monitoring module for MaxScale that monitors a Master-Slave
replication cluster. It assigns master and slave roles inside MaxScale according to
the actual replication tree in the cluster.

## Configuration

A minimal configuration for a  monitor requires a set of servers for monitoring
and a username and a password to connect to these servers.

```
[MyMonitor]
type=monitor
module=mariadbmon
servers=server1,server2,server3
user=myuser
passwd=mypwd

```
Note that from MaxScale 2.2.1 onwards, the module name is `mariadbmon`; up until
MaxScale 2.2.0 it was `mysqlmon`. The name `mysqlmon` has been deprecated but can
still be used, although it will cause a warning to be logged.

The user requires the REPLICATION CLIENT privilege to successfully monitor the
state of the servers.

```
MariaDB [(none)]> grant replication client on *.* to 'maxscale'@'maxscalehost';
Query OK, 0 rows affected (0.00 sec)
```

## Common Monitor Parameters

For a list of optional parameters that all monitors support, read the
[Monitor Common](Monitor-Common.md) document.

## MariaDB Monitor optional parameters

These are optional parameters specific to the MariaDB Monitor.

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
functionality. The only difference is that the MariaDB monitor will also detect
traditional Master-Slave topologies.

### `ignore_external_masters`

Ignore any servers that are not monitored by this monitor but are a part of the
replication topology. This option was added in MaxScale 2.1.12 and is disabled
by default.

MaxScale detects if a master server replicates from an external server. When
this is detected, the server is assigned the `Slave` and `Slave of External
Server` labels and will be treated as a slave server. Most of the time this
topology is used when MaxScale is used for read scale-out without master
servers, a Galera cluster with read replicas being a prime example of this
setup. Sometimes this is not the desired behavior and the external master server
should be ignored. Most of the time this is due to multi-source replication.

When this option is enabled, all servers that have the `Master, Slave, Slave of
External Server, Running` labels will instead get the `Master, Running` labels.

### `detect_standalone_master`

Detect standalone master servers. This feature takes a boolean parameter and
from MaxScale 2.2.1 onwards is enabled by default. Up until MaxScale 2.2.0 it
was disabled by default. In MaxScale 2.1.0, this parameter was called `failover`.

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

This mode in mariadbmon is completely passive in the sense that it does not modify
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

If automatic failover is enabled (`auto_failover=true`), this setting also
controls how many times the master server must fail to respond before failover
begins.

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

## Failover, switchover and auto-rejoin

Starting with MaxScale 2.2.1, MariaDB Monitor supports replication cluster
modification. The operations implemented are: _failover_ (replacing a failed
master), _switchover_ (swapping a slave with a running master) and _rejoin_
(joining a standalone server to the cluster). The features and the parameters
controlling them are presented in this section.

These features require that the monitor user (`user`) has the SUPER privilege.
In addition, the monitor needs to know which username and password a slave
should use when starting replication. These are given in `replication_user` and
`replication_password`.

All three operations can be activated manually through MaxAdmin/MaxCtrl. All
commands require the monitor instance name as first parameter. Failover selects
the new master server automatically and does not require additional parameters.
Rejoin requires the name of the joining server as second parameter.

Switchover takes one to three parameters. If only the monitor name is given,
switchover will autoselect both the slave to promote and the current master. If
two parameters are given, the second parameter is interpreted as the slave to
promote. If three parameters are given, the third parameter is interpreted as
the current master. The user-given current master is compared to the master
server currently deduced by the monitor and if the two are unequal, an error is
given.

Example commands are below:

```
call command mariadbmon failover MyMonitor
call command mariadbmon switchover MyMonitor SlaveServ3
call command mariadbmon switchover MyMonitor SlaveServ3 MasterServ
call command mariadbmon rejoin MyMonitor NewServer2
```

The commands follow the standard module command syntax. All require the monitor
configuration name (MyMonitor) as the first parameter. For switchover, the
following parameters define the server to promote (SlaveServ3) and the server to
demote (MasterServ). For rejoin, the server to join (NewServer2) is required.

Failover can activate automatically, if `auto_failover` is on. The activation
begins when the master has been down for a number of monitor iterations defined
in `failcount`.

Rejoin stands for starting replication on a standalone server or redirecting a
slave replicating from the wrong master (any server that is not the cluster
master). The rejoined servers are directed to replicate from the current cluster
master server, forcing the replication topology to a 1-master-N-slaves
configuration.

A server is categorized as standalone if the server has no slave connections,
not even stopped ones. A server is replicating from the wrong master if the
slave IO thread is connected but the master server id seen by the slave does not
match the cluster master id. Alternatively, the IO thread may be stopped or
connecting but the master server host or port information differs from the
cluster master info. These criteria mean that a STOP SLAVE does not yet set a
slave as standalone.

With `auto_rejoin` active, the monitor will try to rejoin any servers matching
the above requirements. Rejoin does not obey `failcount` and will attempt to
rejoin any valid servers immediately. When activating rejoin manually, the
user-designated server must fulfill the same requirements.

### Limitations and requirements

Switchover and failover only understand simple topologies. They will not work if
the cluster has multiple masters, relay masters, or if the topology is circular.
The server cluster is assumed to be well-behaving with no significant
replication lag and all commands that modify the cluster complete in a few
seconds (faster than `backend_read_timeout` and `backend_write_timeout`).

The backends must all use GTID-based replication, and the domain id should not
change during a switchover or failover. Master and slaves must have
well-behaving GTIDs with no extra events on slave servers.

Switchover requires that the cluster is "frozen" for the duration of the
operation. This means that no data modifying statements such as INSERT or UPDATE
are executed and the GTID position of the master server is stable. When
switchover begins, the monitor sets the global *read_only* flag on the old
master backend to stop any updates. *read_only* does not affect users with the
SUPER-privilege so any such user can issue writes during a switchover. These
writes have a high chance to break replication, because the write may not be
replicated to all slaves before they switch to the new master. To prevent this,
any users who commonly do updates should not have the SUPER-privilege. For even
more security, the only SUPER-user session during a switchover should be the
MaxScale monitor user.

When mixing rejoin with failover/switchover, the backends should have
*log_slave_updates* on. The rejoining server is likely lagging behind the rest
of the cluster. If the current cluster master does not have binary logs from the
moment the rejoining server lost connection, the rejoining server cannot
continue replication. This is an issue if the master has changed and
the new master does not have *log_slave_updates* on.

### External master support

The monitor detects if a server in the cluster is replicating from an external
master (a server that is not monitored by the monitor). If the replicating
server is the cluster master server, then the cluster itself is considered to
have an external master.

If a failover/switchover happens, the new master server is set to replicate from
the cluster external master server. The usename and password for the replication
are defined in `replication_user` and `replication_password`. The address and
port used are the ones shown by `SHOW ALL SLAVES STATUS` on the old cluster
master server. In the case of switchover, the old master also stops replicating
from the external server to preserve the topology.

After failover the new master is replicating from the external master. If the
failed old master comes back online, it is also replicating from the external
server. To normalize the situation, either have *auto_rejoin* on or manually
execute a rejoin. This will redirect the old master to the current cluster
master.

### Configuration parameters

#### `auto_failover`

Enable automated master failover. This parameter expects a boolean value and the
default value is false.

When automatic failover is enabled, traditional MariaDB Master-Slave clusters
will automatically elect a new master if the old master goes down and stays down
a number of iterations given in `failcount`. Failover will not take place when
MaxScale is configured as a passive instance. For details on how MaxScale
behaves in passive mode, see the documentation on `failover_timeout` below.

If an attempt at failover fails or multiple master servers are detected, an
error is logged and automatic failover is disabled. If this happens, the cluster
must be fixed manually and the failover needs to be re-enabled via the REST API
or MaxAdmin.

The monitor user must have the SUPER privilege for failover to work.

#### `auto_rejoin`

Enable automatic joining of server to the cluster. This parameter expects a
boolean value and the default value is false.

When enabled, the monitor will attempt to direct standalone servers and servers
replicating from a relay master to the main cluster master server, enforcing a
1-master-N-slaves configuration.

For example, consider the following event series.

1. Slave A goes down
2. Master goes down and a failover is performed, promoting Slave B
3. Slave A comes back

Slave A is still trying to replicate from the downed master, since it wasn't
online during failover. If `auto_rejoin` is on, Slave A will quickly be
redirected to Slave B, the current master.

#### `replication_user` and `replication_password`

The username and password of the replication user. These are given as the values
for `MASTER_USER` and `MASTER_PASSWORD` whenever a `CHANGE MASTER TO` command is
executed.

Both `replication_user` and `replication_password` parameters must be defined if
a custom replication user is used. If neither of the parameters is defined, the
`CHANGE MASTER TO` command will use the monitor credentials for the replication
user.

The credentials used for replication must have the `REPLICATION SLAVE`
privilege.

`replication_password` uses the same encryption scheme as other password
parameters. If password encryption is in use, `replication_password` must be
encrypted with the same key to avoid erroneous decryption.

#### `failover_timeout` and `switchover_timeout`

Time limit for the cluster failover and switchover in seconds. The default values
are 90 seconds.

If no successful failover/switchover takes place within the configured time
period, a message is logged and automatic failover is disabled. This prevents
further automatic modifications to the misbehaving cluster.

`failover_timeout` also controls how long a MaxScale instance that has
transitioned from passive to active will wait for a failover to take place after
an apparent loss of a master server. If no new master server is detected within
the configured time period, failover will be initiated again.

#### `verify_master_failure` and `master_failure_timeout`

Enable additional master failure verification for automatic failover.
`verify_master_failure` is a boolean value (default: true) which enables this
feature and `master_failure_timeout` defines the timeout in seconds (default: 10).

The failure verification is performed by checking whether the slaves are still
connected to the master and receiving events. Effectively, if a slave has
received an event within `master_failure_timeout` seconds, the master is not
considered down when deciding whether to auto_failover.

If every slave loses its connection to the master (*Slave_IO_Running* is not
"Yes"), master failure is considered verified regardless of timeout. This allows
a faster failover when the master server crashes, as that causes immediate
disconnection.

For automatic failover to activate, the `failcount` requirement must also be
met.

#### `servers_no_promotion`

This is a comma-separated list of server names that will not be chosen for
master promotion during a failover or autoselected for switchover. This does not
affect switchover if the user selects the server to promote. Using this setting
can disrupt new master selection for failover such that an nonoptimal server is
chosen. At worst, this will cause replication to break. Alternatively, failover
may fail if all valid promotion candidates are in the exclusion list.

```
servers_no_promotion=backup_dc_server1,backup_dc_server2
```

### Manual activation

Failover, switchover and rejoin can be activated manually through the REST API
or MaxAdmin. The commands are only performed when MaxScale is in active mode.

It is safe to perform manual operations even with `auto_failover` on, since
the automatic operations cannot happen simultaneously with the manual one.

If a switchover or failover fails, automatic failover is disabled to prevent
master changes to a possibly malfunctioning cluster. Automatic failover can be
turned on manually via the REST API or MaxAdmin. Example commands are listed
below.

```
maxadmin alter monitor MariaDB-Monitor auto_failover=true
maxctrl alter monitor MariaDB-Monitor auto_failover true
```

If automatic rejoin fails, it is disabled. To re-enable, use a similar command
as with automatic failover, replacing `auto_failover` with `auto_rejoin`.

When switchover is iniated via the REST-API, the URL path is:
```
/v1/maxscale/mariadbmon/switchover?<monitor-instance>&<new-master>&<current-master>
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
module=mariadbmon
servers=server1, server2, server3, server 4
...
```
with the assumption that `server2` is the current master, then the URL
path for making `server4` the new master would be:
```
/v1/maxscale/mariadbmon/switchover?Cluster1&server4&server2
```

The REST-API paths for manual failover and manual rejoin are mostly similar.
Failover does not accept any server parameters, rejoin requires the name of the
joining server.
```
/v1/maxscale/mariadbmon/failover?Cluster1
/v1/maxscale/mariadbmon/rejoin?Cluster1&server3
```

## Using the MariaDB Monitor With Binlogrouter

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

Here is a monitor configuration that only triggers the script when a master
or a slave server goes down.

```
[Database Monitor]
type=monitor
module=mariadbmon
servers=server1,server2
script=mail_to_admin.sh
events=master_down,slave_down
```

When a master or a slave server goes down, the script is executed, a mail is
sent and the administrator will be immediately notified of any possible
problems.  This is just a simple example showing what you can do with MaxScale
and monitor scripts.
