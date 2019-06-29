# MariaDB MaxScale 2.4.0 Release Notes -- 2019-06-29

Release 2.4.0 is a Beta release.

This document describes the changes in release 2.4.0, when compared to
release 2.3.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Section and object names

Section and object names starting with `@@` are now reserved for
use by MaxScale itself. If any such names are encountered in
configuration files, then MaxScale will not start.

Whitespace in section names that was deprecated in 2.2 will now be
rejected and cause the startup of MaxScale to fail.

### Binding on network ports

MaxScale 2.4.0 will now use the SO_REUSEPORT capability offered by newer kernels
that allows reuse of network listener ports. In practice this means improved
connection creation speed with more dynamic balancing of connections.

As a side-effect of this, it is possible for two MaxScale instances to bind on
the same listener port on systems that have Linux kernels newer than 3.9. This
can only happen if the MaxScale instances use completely different directory
structures (i.e. different `--basedir` arguments). Normal use of MaxScale still
detects multiple MaxScales trying to bind to the same ports. Almost always, this
will not have any negative side-effects.

### Stronger hashing algorithm for admin user passwords

The administrative user passwords are now stored as SHA2-512 hashes which is an
improvement over the older MD5 hashing algorithm. New users will use the
stronger algorithm but old users will continue using the weaker one. To upgrade
administrative users, recreate the user.

### REST API

#### Mandatory `protocol` parameter on server creation

The `protocol` parameter must now always be defined when a server is
created. The previously undocumented default value of `mariadbbackend` now must
be explicitly defined when a server is created via the REST API.

#### TLS on server creation

To create encrypted connection to a server, the TLS parameters must be defined
at server creation time. To enable TLS for a server that doesn't have it,
destroy the old one and recreate it afterwards.

## Dropped Features

### Enabling server TLS via MaxAdmin

As TLS for servers must now be defined at creation time, enabling TLS at runtime
via MaxAdmin is no longer possible. Use MaxCtrl to create servers with TLS
enabled.

### `debugcli` and `telnetd`

The `debugcli` router and the `telnetd` protocol module it uses have been
removed.

### `ndbclustermon`

The `ndbclustermon` module has been removed.

### `mmmon`

The `mmmon` module has been removed as the `mariadbmon` monitor largely does
what it used to do.

### MariaDB-Monitor settings

The following settings have been removed and cause a startup error
if defined: `mysql51_replication`, `multimaster` and `allow_cluster_recovery`.

### `log_to_shm`

The `log_to_shm` parameter that was removed in 2.3 will be treated as an unknown
parameter in 2.4.0.

## Deprecated Features

### `mqfilter`

The `mqfilter` has been deprecated and it will be removed in a future version
of MaxScale.

We advise against using it.

### Nagios Plugins

MaxScale no longer ships the example scripts and configuration files for Nagios.

## New Features

### Clustrix Support

MaxScale now contains support for Clustrix in the form of a Clustrix monitor
that is capable of monitoring a Clustrix cluster.

Please see the
[documentation](../Monitors/Clustrix-Monitor.md) for details.

### Smart Router

MaxScale has now a new router _SmartRouter_ that is capable of routing a query
to different kinds of backends, containing the same data, depending on which
backend can best handle that particular kind of query.

Please see the
[documentation](../Routers/SmartRouter.md) for details.

### Servers can be drained

It is now possible to drain a server, which means that existing
connections to the server can continue to be used but new connections
are no longer created to the server.

In the output of `maxctrl`, the fact that a server is being drained
is visible in the `State` column as the value `Draining`.
```
┌─────────┬─────────────────┬──────┬─────────────┬───────────────────────────────┬───────┐
│ Server  │ Address         │ Port │ Connections │ State                         │ GTID  │
├─────────┼─────────────────┼──────┼─────────────┼───────────────────────────────┼───────┤
│ Server1 │ 192.168.121.159 │ 3306 │ 2           │ Master, Running               │ 0-1-6 │
├─────────┼─────────────────┼──────┼─────────────┼───────────────────────────────┼───────┤
│ Server2 │ 192.168.121.80  │ 3306 │ 1           │ Draining, Slave, Running      │ 0-1-6 │
├─────────┼─────────────────┼──────┼─────────────┼───────────────────────────────┼───────┤
│ Server3 │ 192.168.121.122 │ 3306 │ 2           │ Slave, Running                │ 0-1-6 │
├─────────┼─────────────────┼──────┼─────────────┼───────────────────────────────┼───────┤
│ Server4 │ 192.168.121.144 │ 3306 │ 2           │ Slave, Running                │ 0-1-6 │
└─────────┴─────────────────┴──────┴─────────────┴───────────────────────────────┴───────┘
```
A server is set in the _Draining_ state the same way as it is
set in the _Maintenance_ state:
```
$ maxctrl set server Server2 drain
```
Note that although the state is displayed as `Draining`, when setting
and clearing the state, the word `drain` is used.

Note that the full implication of draining a server depends upon
both on the role of the server and on the router being used, and its
configuration.

For instance, if readwritesplit is used and the server being drained
is a slave, then from a client's perspective there will be no difference;
readwritesplit will simply not use that server. However, if the server
being drained is the master, then it will not be possible to connect
unless `master_failure_mode` has been set to something else but the
default `fail_instantly`.

Once the server has been drained, the state will be `Drained`.

### `weightby` Replacement for Servers: `rank`

The new [`rank`](../Getting-Started/Configuration-Guide.md#rank) parameter is
the replacement for the deprecated `weightby` parameter. It allows explicit
groupings of servers into primary and secondary groups. Servers configured with
`rank=secondary` will only be used if no primary servers are available.

### UNIX Domain Socket for Servers

Servers can now use the
[`socket`](../Getting-Started/Configuration-Guide.md#socket) parameter to define
a local UNIX domain socket through which the connections will be created.

### Cluster

The servers a service uses can now be specified using the `cluster`
parameter of the service.
```
[TheService]
...
cluster=TheMonitor
```
In this case, the servers of the service will be defined by the
referred to monitor. Note that the parameters `servers` and `cluster`
are mutually exclusive.

### Durations

In the MaxScale configuration file, durations can now be suffixed with
`h`, `m`, `s` or `ms` to indicate that the duration is specified as
hours, minutes, seconds or milliseconds.

Please see the
[configuration guide](../Getting-Started/Configuration-Guide.md#durations)
for details.

_Not_ providing an explicit unit is strongly discouraged as it will be
deprecated in MaxScale 2.5.

### Query Classifier Cache

It is now possible to examine the contents of the query classifier cache.
The REST-API endpoint is
```
/v1/maxscale/query_classifier/cache
```
and the equivalent _maxctrl_ command
```
maxctrl show qc_cache
```
The output shows the statements (the canonical version) in the cache,
the number of times they have been encountered and how they have been
classified.

### Connection Attempt Throttling

If a user fails to authenticate multiple times, the host from where the user is
connecting from will be blocked for 60 seconds. See
[`max_auth_errors_until_block`](../Getting-Started/Configuration-Guide.md#max_auth_errors_until_block)
for more information.

### REST API & MaxCtrl

#### Default API Version

The API version prefix is now optional and if not present, will be assumed to be
the latest version which currently is `/v1`.

#### Hard maintenance mode

The new `--force` option for the `set server` command in MaxCtrl allows all
connections to the server in question to be closed when it is set into
maintenance mode. This causes idle connections to be closed immediately.

For more information, read the
[REST-API](../REST-API/Resources-Server.md#set-server-state) documentation for
the `set` endpoint.

#### Command History

The interactive mode for MaxCtrl now has command history.

#### Multi-parameter Alter

The `alter` commands in MaxCtrl now accept multiple key-value pairs in one
command. See output of `maxctrl help alter` for more information.

### Readwritesplit

For more information on the readwritesplit router, refer to the
[documentation](../Routers/ReadWriteSplit.md).

#### `transaction_replay`

The transaction replay functionality will now also be applied in conjunction
with server initiated transaction rollbacks.

#### `transaction_replay_attempts`

The new `transaction_replay_attempts` parameter controls how many errors the
transaction replay mechanism tolerates before giving up on the replay
attempt. The number of transaction replay attempts is now capped to a default
value of 5.

#### `lazy_connect`

Lazy connection creation delays the opening of all connections until they are
needed. This reduces the load that is placed on the backend servers when the
client connections are short. This feature is disabled by default.

#### Connection Selection

The servers where new connections are created at the start of a session are now
always use connection counts. This allows the use of
`slave_selection_criteria=LEAST_CURRENT_OPERATIONS` and
`max_slave_connections=1`.

#### Master Selection

Readwritesplit will now load balance master connections in case there are
multiple master servers. This is mainly of relevance only with Clustrix
clusters.

#### Maintenance mode

Readwritesplit now allows open transactions to finish if the master is put into
maintenance mode. To forcefully close all connections to a server use the
`maxctrl set server <name> maintenance --force` command.

### Galeramon

#### Replicating Slaves

If a slave server is replicating from a Galera node, galeramon will now
correctly assign it the Slave status.

#### GTID in `list servers`

Galera nodes will now display their GTID positions in the output of
`maxctrl list servers`.

### Avrorouter Direct Replication

By defining the `servers` parameter for the avrorouter service, the replication
is done directly from a remote master server. This skips the binlogrouter
definition completely making the conversion process faster and more space
efficient.

### `enforce_simple_topology`

This MariaDB-Monitor setting allows the monitor greater freedom in managing the
backend servers. Please see
[MariaDB-Monitor documentation](../Monitors/MariaDB-Monitor.md#enforce_simple_topology)
for more information.

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.4.0.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.4.0)

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is X.Y.Z. Further, *master* always refers to the latest released non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
