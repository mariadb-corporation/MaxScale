# MariaDB MaxScale 2.3.0 Release Notes -- 2018-10-09

Release 2.3.0 is a Beta release.

This document describes the changes in release 2.3.0, when compared to
release 2.2.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### maxadmin `list threads`

Maxadmin `list threads` now shows the descriptor counts and load of
the MaxScale worker threads. For details, please consult the
maxadmin [documentation](../Reference/MaxAdmin.md).

### MaxCtrl `create monitor`

The `create monitor` command now accepts a list of key-value parameters that are
passed to the monitor as the last argument. This allows creation and
configuration of monitors in one command.

### `query_retries`

The default value of `query_retries` was changed from 0 to 1. This turns
on failure tolerant queries for monitors and authenticators which prevents
failures due to network problems. A single reconnection attempt is made to
keep responsiveness high even with default values.

### Session Command History

The _readwritesplit_ session command history is now enabled by default but it
is limited to a total of 50 distict session commands. This default allows most
sessions to leverage the newly improved reconnection mechanism without having to
explicitly enable the history. When the limit of 50 commands is exceeded, the
session command history is disabled. This makes it possible to use pooled
connections by default without having to explicitly disable the history (this
was the case with pre-2.1 versions of MaxScale).

The way that the history is stored has also changed. Instead of storing all
session commands, each session only stores the first and last execution of each
command. This way the history is compressed into a compact representation while
still retaining the relative order of each command.

To keep the old functionality, add `disable_sescmd_history=true` to the service
definition.

### Cache Filter

The rules can now be expressed using a JSON array containing rule objects,
which makes it easier to express more complex rules. Please see the
[Cache](../Filters/Cache.md) documentation for details.

### Masking Filter

By default the masking filter rejects statements that use functions on
conjuction with columns that should be masked. Please see the
[Masking Filter](../Filters/Masking.md) documentation for details.

### `router_options` in Binlogrouter

The use of `router_options` with the binlogrouter module is deprecated in
MaxScale 2.3 and will be removed in a subsequent release. The
`router_options` should be converted into separate parameters.

### `thread_stack_size`

The `thread_stack_size` configuration parameter is ignored and has been
deprecated. If you need to explicitly set the stack size, do so using
`ulimit -s` before starting MaxScale.

### `ssl` for Servers and Listeners

The `ssl` parameter now accepts boolean values ìn addition to the old `required`
and `disabled` values.

### MariaDBMonitor

MariaDBMonitor has undergone several changes listed briefly below. Please see
[MariaDBMonitor documentation](../Monitors/MariaDB-Monitor.md) for more details.

#### JSON diagnostics output changed

The data in the diagnostic output of the REST-API has changed, with some fields
removed and others added.

#### Master detection

The monitor is now less likely to suddenly change the master server, even if
another server has more slaves than the current master. The DBA can force a
master reselection by setting the current master read-only, or by removing all
its slaves if the master is down.

Only one server can have the *Master* status flag at a time, even in a
multimaster setup. Others servers in the multimaster group are given the *Relay
Master* and *Slave* status flags.

####  Switchover new master autoselection

The switchover command can now be called with just the monitor instance name as
parameter. In this case the monitor will automatically select a server for
promotion.

#### Replication lag detection

The replication lag measurement now simply reads the
*Seconds_Behind_Master*-field of the slave status output of slaves. The slave
calculates this value by comparing the time stamp in the binlog event the slave
is currently processing to the slave's own clock. If a slave has multiple slave
connections, the smallest lag is used.

#### Low disk space detection, automatic switchover

With recent MariaDB Server versions, the monitor can check the disk space on the
backends and detect if the server is running low. The monitor can be set to
automatically switchover a master low on disk space. Slaves can be set to
maintenance mode. Disk space is also considered when selecting a new master for
promotion. See
[switchover_on_low_disk_space](../Monitors/MariaDB-Monitor.md#switchover_on_low_disk_space)
and
[maintenance_on_low_disk_space](../Monitors/MariaDB-Monitor.md#maintenance_on_low_disk_space)
for more information.

#### Replication reset feature

The *reset-replication* monitor command deletes all slave connections and binary
logs, and then sets up replication. Useful when data is in sync but gtid:s are
not.

#### Scheduled events handling in failover/switchover/rejoin

Server events lauched by the event scheduler thread are now handled during
cluster modification operations. See
[handle_server_events](../Monitors/MariaDB-Monitor.md#handle_server_events)
for more information.

#### Unused parameters

The following parameters are unused and are ignored if set:
- `mysql51_replication` MySQL5.1 is no longer supported.
- `multimaster` The monitor now always detects multimaster setups.
- `allow_cluster_recovery` Now always on.
- `detect_replication_lag` Lag detection no longer writes to databases so it is
always on.

## Dropped Features

### `log_to_shm` parameter and `--log=shm` option

The following commands no longer create the log files in shared memory and will
be ignored. MaxScale will behave as if `--log=file` was provided when
`--log=shm` is used. The `log_to_shm` parameter is ignored. Both the parameter
and the optino value are deprecated and will be removed in a later relesae.

Logs can still be created in shared memory by pointing `logdir` to `/dev/shm`.

### Configuration Reloading

The deprecated `maxadmin reload config` command has been removed.

### `router_options` in Avrorouter

The use of `router_options` with avrorouter was deprecated in MaxScale 2.1. In
MaxScale 2.3, the use of `router_options` is no longer supported and the options
should be given as parameters instead.

### `router_options` in readwritesplit

The use of `router_options` with readwritesplit, which was deprecated in
MaxScale 2.2.0, has been removed in MaxScale 2.3.0.

### `QUERY-LAST-TRANSACTION` and `QUERY-TRANSACTION` CDC commands

The CDC protocol no longer accepts the `QUERY-LAST-TRANSACTION` and
`QUERY-TRANSACTION` commands. They were removed due to the addition of the REST
API that provides the same information in a more easy to process format.

## New Features

### Binlog Router

If the binlog router is replicating from a Galera cluster it is now
possible to specify secondary masters that the binlog router automatically
will switch to, in case the primary master for some reason goes down.
Please see the binlog router
[documentation](../Routers/Binlogrouter.md)
for details.

### Hints

There is now a new hint `last` that will cause a query to be routed to the
same server the previous query was routed to.

### Backend Connection IDs

The backend connection IDs are now shown in the `show sessions` output of
MaxCtrl.

### Comment Filter

With the comment filter it is possible to prepend statement received
with a comment before it is sent further to a server. Please see the
[comment filter documentation](../Filters/Comment.md)
for more details.

### Query Classifier Cache

The query classifier now caches the results of each classification which
significantly improves performance of workloads that require query
classification. The default cache size limit is 40% of total system memory.

Read [the configuration guide](../Getting-Started/Configuration-Guide.md#query_classifier_cache_size)
for more information about the details of the query classifier cache and how to
configure it.

### Runtime Configuration of the Cache

With the variables `@maxscale.cache.use` and `@maxscale.cache.populate`
it is now possible for a client to specify whether the cache should be
used and/or populated. Please see the
[Cache](../Filters/Cache.md) documentation for details.

### User Specified Syslog Facility and Level for Authentication Errors

It is now possible for the end user to specify the syslog facility and level
for authentication errors. Please see
[the configuration guide](../Getting-Started/Configuration-Guide.md#events)
for details.

### Named Server Filter

The `source` parameter can now contain a list of comma separated addresses.

### Table Family Sharding

The SchemaRouter is now capable of table family sharding. Please see the
SchemaRouter [documentation](../Routers/SchemaRouter.md) for details.

### Throttle filter
The [throttlefilter](../Filters/Throttle.md) replaces and extends on the limit_queries
functionality of [the Database Firewall filter](../Filters/Database-Firewall-Filter.md).

### ReadWriteSplit

A set of new features have been added to readwritesplit.

#### `transaction_replay`

The [`transaction_replay`](../Routers/ReadWriteSplit.md#transaction_replay)
parameter enables replaying of transactions if a master server is lost
mid-transaction. This allows transparent replacement of master servers with a
minimal amount of failed transactions.

#### `master_reconnection`

With the
[`master_reconnection`](../Routers/ReadWriteSplit.md#master_reconnection)
parameter, if the connection to a master server is lost or the master server
changes, readwritesplit can now reconnect seamlessly to the master server
without losing the session state.

#### `delayed_retry`

The [`delayed_retry`](../Routers/ReadWriteSplit.md#delayed_retry) parameter
allows queries to be automatically retried if their execution is interrupted.

#### `causal_reads`

The [`causal_reads`](../Routers/ReadWriteSplit.md#causal_reads) parameter
enables distributed consistent reads with MariaDB version 10.2 and newer.

#### `optimistic_trx`

The [`optimistic_trx`](../Routers/ReadWriteSplit.md#optimistic_trx) parameter
enables optimistic transaction execution. This parameter controls whether normal
transactions (i.e. `START TRANSACTION` or `BEGIN`) are load balanced across
slaves. If the transaction tries to modify a row, it is migrated to the master
and rolled back on the slave.

#### Adaptive Routing

A new load balancing method,
[`ADAPTIVE_ROUTING`](../Routers/ReadWriteSplit.md#slave_selection_criteria), was
added. It uses the average query response time to distribute the load so that it
is optimal on all servers.

### MaxCtrl

For more detailed information about the features of MaxCtrl, see the
[documentation](../Reference/MaxCtrl.md) or the output of `maxctrl help`.

#### Interactive Mode for MaxCtrl

MaxCtrl can now be started in an interactive mode similar to MaxAdmin. This
makes use of passwords more convenient as they have to be input only once and
they are cached for the duration of the session.

#### Draining Server Connections

The new `drain server` drains the server of connections by first removing it
from all services after which it waits until all connections are closed. When
all connections are closed, the server is put into the `maintenance` state and
added back to all the services where it was removed from.

### Resultset Concatenation Router

A new experimental router module, `cat`, was added to the
`maxscale-experimental` package. The `cat` router is a special router that
concatenates result sets. For more information and an explanation on how the
router works, read the [`cat` documentation](../Routers/Cat.md).

### REST API Additions

#### Creation of Services and Filters at Runtime

The REST API and MaxCtrl now support the creation and destruction of services
and filters at runtime. This also means that the filters of a service can now be
modified at runtime. Refer to the [REST API](../REST-API/API.md) and
[MaxCtrl](../Reference/MaxCtrl.md) documentation for more details.

#### Alteration of Routers

The router parameters of services can now be altered at runtime. Currently only
the readwritesplit router implements this feature.

### Binlog Filtering

The `binlogfilter` is a filter module that allows the client side stream of
binlogs to be filtered. It is designed to be used with the binlogrouter.

This allows conditional replication similar to _replicate_do_table_ but with
full PCRE2 compliant match/exclude functionality. For more information, refer to
the [binlogfilter](../Filters/BinlogFilter.md) documentation.

### Network Traffic Buffering

The new
[`writeq_high_water`](../Getting-Started/Configuration-Guide.md#writeq_high_water)
and
[`writeq_low_water`](../Getting-Started/Configuration-Guide.md#writeq_low_water)
parameters allow network traffic to be throttled if it exceeds the highwater
mark. This can be used to prevent MaxScale from buffering too much data in
memory if the client is not reading results fast enough.

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.3.0.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.3.0)

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
