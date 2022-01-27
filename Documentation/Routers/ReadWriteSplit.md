# Readwritesplit

This document provides a short overview of the **readwritesplit** router module
and its intended use case scenarios. It also displays all router configuration
parameters with their descriptions. A list of current limitations of the module
is included and use examples are provided.

[TOC]

## Overview

The **readwritesplit** router is designed to increase the read-only processing
capability of a cluster while maintaining consistency. This is achieved by
splitting the query load into read and write queries. Read queries, which do not
modify data, are spread across multiple nodes while all write queries will be
sent to a single node.

The router is designed to be used with a traditional Master-Slave replication
cluster. It automatically detects changes in the master server and will use the
current master server of the cluster. With a Galera cluster, one can achieve a
resilient setup and easy master failover by using one of the Galera nodes as a
Write-Master node, where all write queries are routed, and spreading the read
load over all the nodes.

## Interaction with servers in `Maintenance` and `Draining` state

When a server that readwritesplit uses is put into maintenance mode, any ongoing
requests are allowed to finish before the connection is closed. If the server
that is put into maintenance mode is a master, open transaction are allowed to
complete before the connection is closed. Note that this means neither idle
session nor long-running transactions will be closed by readwritesplit. To
forcefully close the connections, use the following command:

```
maxctrl set server <server> maintenance --force
```

If a server is put into the `Draining` state while a connection is open, the
connection will be used normally. Whenever a new connection needs to be created,
whether that be due to a network error or when a new session being opened, only
servers that are neither `Draining` nor `Drained` will be used.

## Configuration

Readwritesplit router-specific settings are specified in the configuration file
of MariaDB MaxScale in its specific section. The section can be freely named but
the name is used later as a reference in a listener section.

For more details about the standard service parameters, refer to the
[Configuration Guide](../Getting-Started/Configuration-Guide.md).

Starting with 2.3, all router parameters can be configured at runtime. Use
`maxctrl alter service` to modify them. The changed configuration will only be
taken into use by new sessions.

## Parameters

### `max_slave_connections`

- **Type**: integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 255

`max_slave_connections` sets the maximum number of slaves a router session uses
at any moment. The default is to use at most 255 slave connections per client
connection. In older versions the default was to use all available slaves with
no limit.

For MaxScale 2.5.12 and newer, the minimum value is 0.

For MaxScale versions 2.5.11 and older, the minimum value is 1. These versions
suffer from a bug ([MXS-3536](https://jira.mariadb.org/browse/MXS-3536)) that
causes the parameter to accept any values but only function when a value greater
than one was given.

Starting with MaxScale 2.5.0, the use of percentage values in
`max_slave_connections` is deprecated. The support for percentages will be
removed in a future release.

For example, if you have configured MaxScale with one master and three slaves
and set `max_slave_connections=2`, for each client connection a connection to
the master and two slave connections would be opened. The read query load
balancing is then done between these two slaves and writes are sent to the
master.

By tuning this parameter, you can control how dynamic the load balancing is at
the cost of extra created connections. With a lower value of
`max_slave_connections`, less connections per session are created and the set of
possible slave servers is smaller. With a higher value in
`max_slave_connections`, more connections are created which requires more
resources but load balancing will almost always give the best single query
response time and performance. Longer sessions are less affected by a high
`max_slave_connections` as the relative cost of opening a connection is lower.

### `slave_connections`

- **Type**: integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 255

This parameter controls how many slave connections each new session starts
with. The default value is 255 which is the same as the default value of
`max_slave_connections`.

In contrast to `max_slave_connections`, `slave_connections` serves as a
soft limit on how many slave connections are created. The number of slave
connections can exceed `slave_connections` if the load balancing algorithm
finds an unconnected slave server better than all other slaves.

Setting this parameter to 1 allows faster connection creation and improved
resource usage due to the smaller amount of initial backend
connections. It is recommended to use `slave_connections=1` when the
lifetime of the client connections is short.

### `max_slave_replication_lag`

- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 0s

Specify how many seconds a slave is allowed to be behind the master. The lag of
a slave must be less than the configured value in order for it to be used for
routing. If set to 0 (the default value), the feature is disabled.

In MaxScale 2.5.0, the slave lag must be less than `max_slave_replication_lag`
whereas in older versions the slave lag had to be less than or equal to
`max_slave_replication_lag`. This means that in MaxScale 2.5.0 it is possible to
define, with `max_slave_replication_lag=1`, that all slaves must be up to date
in order for them to be used for routing.

Note that this feature does not guarantee that writes done on the master are
visible for reads done on the slave. This is mainly due to the method of
replication lag measurement. For a feature that guarantees this, refer to
[`causal_reads`](#causal_reads).

The lag is specified as documented
[here](../Getting-Started/Configuration-Guide.md#durations). If no explicit unit
is provided, the value is interpreted as seconds in MaxScale 2.4. In subsequent
versions a value without a unit may be rejected. Note that since the granularity
of the lag is seconds, a lag specified in milliseconds will be rejected, even if
the duration is longer than a second.

The Readwritesplit-router does not detect the replication lag itself. A monitor
such as the MariaDB-monitor for a Master/Slave-cluster is required. This option
only affects Master-Slave clusters. Galera clusters do not have a concept of
slave lag even if the application of write sets might have lag. When a server is
disqualified from routing because of replication lag, a warning is logged. Similarly,
when the server has caught up enough to be a valid routing target, another warning
is logged. These messages are only logged when a query is being routed and the
replication state changes.

### `use_sql_variables_in`

- **Type**: enum
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `master`, `all`
- **Default**: `all`

**`use_sql_variables_in`** specifies where should queries, which read session
variable, be routed. The syntax for `use_sql_variable_in` is:

    use_sql_variables_in=[master|all]

The default is to use SQL variables in all servers.

When value `all` is used, queries reading session variables can be routed to any
available slave (depending on selection criteria). Queries modifying session
variables are routed to all backend servers by default, excluding write queries
with embedded session variable modifications, such as:

    INSERT INTO test.t1 VALUES (@myid:=@myid+1)

In above-mentioned case the user-defined variable would only be updated in the
master where the query would be routed to due to the `INSERT` statement.

```
[Splitter-Service]
type=service
router=readwritesplit
servers=dbserv1, dbserv2, dbserv3
user=maxscale
password=96F99AA1315BDC3604B006F427DD9484
disable_sescmd_history=true
master_failure_mode=fail_on_write
```

### `connection_keepalive`

** Note: ** This parameter has been moved into the MaxScale core. For the
   current documentation, read the
   [`connection_keepalive`](../Getting-Started/Configuration-Guide.md#connection_keepalive)
   section in the configuration guide.

Send keepalive pings to backend servers. This feature was introduced in MaxScale
2.2.0. The default value is 300 seconds starting with 2.3.2 and for older
versions the feature was disabled by default. This parameter was converted into
a service parameter in MaxScale 2.5.0.

### `master_reconnection`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Allow the master server to change mid-session. This feature was introduced in
MaxScale 2.3.0 and is disabled by default.

When a readwritesplit session starts, it will pick a master server as the
current master server of that session. By default, when this master server
changes mid-session, the connection will be closed.

If the `master_reconnection` parameter is enabled, the master server is allowed
to change as long as the session meets the following criteria:

* The session is already connected to the slave that was chosen to be the new master
* No transaction is open
* Autocommit is enabled
* No `LOAD DATA LOCAL INFILE` is in progress
* There are no queries being actively routed to the old master

When `master_reconnection` is enabled in conjunction with either
`master_failure_mode=fail_on_write` or `master_failure_mode=error_on_write`, the
session can recover from the loss of a master server. This means that when a
session starts without a master server and later a slave server that it is
connected to is promoted as the master, the session will come out of the
read-only mode (described in detail in the
[`master_failure_mode`](#master_failure_mode) documentation).

### `slave_selection_criteria`

- **Type**: enum
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `LEAST_CURRENT_OPERATIONS`, `ADAPTIVE_ROUTING`, `LEAST_BEHIND_MASTER`, `LEAST_ROUTER_CONNECTIONS`, `LEAST_GLOBAL_CONNECTIONS`
- **Default**: `LEAST_CURRENT_OPERATIONS`

This option controls how the readwritesplit router chooses the slaves it
connects to and how the load balancing is done. The default behavior is to route
read queries to the slave server with the lowest amount of ongoing queries i.e.
`LEAST_CURRENT_OPERATIONS`.

The option syntax:

```
slave_selection_criteria=<criteria>
```

Where `<criteria>` is one of the following values.

* `LEAST_GLOBAL_CONNECTIONS`, the slave with least connections from MariaDB MaxScale
* `LEAST_ROUTER_CONNECTIONS`, the slave with least connections from this service
* `LEAST_BEHIND_MASTER`, the slave with smallest replication lag
* `LEAST_CURRENT_OPERATIONS` (default), the slave with least active operations
* `ADAPTIVE_ROUTING`, based on server average response times. See below.

The `LEAST_GLOBAL_CONNECTIONS` and `LEAST_ROUTER_CONNECTIONS` use the
connections from MariaDB MaxScale to the server, not the amount of connections
reported by the server itself.

`LEAST_BEHIND_MASTER` and `ADAPTIVE_ROUTING` do not take server weights into account
when choosing a server.

`ADAPTIVE_ROUTING` Measures average server response times. The server averages
are used as proxies of server load conditions. At selection time the averages
are copied and modified to favor faster servers, while at the same time
guaranteeing at lest some traffic to the slowest servers. The server selection
is probabilistic based on roulette wheel selection.

#### Server Weights and `slave_selection_criteria`

NOTE: Server Weights have been deprecated in MaxScale 2.3 and will be removed
at a later time.

The following formula is used to calculate a score for a server when the
`weightby` parameter is defined.

```
score = x / w
```

`x` is the absolute value of the chosen metric (queries, connections) and
`w` is the weight of the server. The value of `w` is the relative weight
of the server in relation to all the servers configured for the
service. The server with the highest score that fulfills all other
criteria is chosen as the target server.

Read the [configuration guide](../Getting-Started/Configuration-Guide.md#weightby)
for a more detailed example on how the weights are calculated.

For `LEAST_CURRENT_OPERATIONS`, the metric is number of active queries on
the candidate server, for `LEAST_GLOBAL_CONNECTIONS` and
`LEAST_ROUTER_CONNECTIONS` it is the number of open connections and for
`LEAST_BEHIND_MASTER` it is the number of seconds a server is behind the
master.

#### Interaction Between `slave_selection_criteria` and `max_slave_connections`

Depending on the value of `max_slave_connections`, the slave selection criteria
behave in different ways. Here are a few example cases of how the different
criteria work with different amounts of slave connections.

* With `slave_selection_criteria=LEAST_GLOBAL_CONNECTIONS` and
`max_slave_connections=1`, each session picks one slave and one master

* With `slave_selection_criteria=LEAST_CURRENT_OPERATIONS` and
`max_slave_connections=100%`, each session picks one master and as many slaves
as possible

* With `slave_selection_criteria=LEAST_CURRENT_OPERATIONS` each read is load
balanced based on how many queries are active on a particular slave

* With `slave_selection_criteria=LEAST_GLOBAL_CONNECTIONS` each read is sent to
the slave with the least amount of connections

### `max_sescmd_history`

This parameter has been moved to
[the MaxScale core](../Getting-Started/Configuration-Guide.md#max_sescmd_history)
in MaxScale 6.0.

### `disable_sescmd_history`

This parameter has been moved to
[the MaxScale core](../Getting-Started/Configuration-Guide.md#disable_sescmd_history)
in MaxScale 6.0.

### `prune_sescmd_history`

This parameter has been moved to
[the MaxScale core](../Getting-Started/Configuration-Guide.md#prune_sescmd_history)
in MaxScale 6.0.

### `master_accept_reads`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

**`master_accept_reads`** allows the master server to be used for reads. This is
a useful option to enable if you are using a small number of servers and wish to
use the master for reads as well.

By default, no reads are sent to the master as long as there is a valid slave
server available. If no slaves are available, reads are sent to the master
regardless of the value of `master_accept_reads`.

```
# Use the master for reads
master_accept_reads=true
```

### `strict_multi_stmt`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

This option is disabled by default since MaxScale 2.2.1. In older versions, this
option was enabled by default.

When a client executes a multi-statement query, it will be treated as if it were
a DML statement and routed to the master. If the option is enabled, all queries
after a multi-statement query will be routed to the master to guarantee a
consistent session state.

If the feature is disabled, queries are routed normally after a multi-statement
query.

**Warning:** Enable the strict mode only if you know that the clients will send
  statements that cause inconsistencies in the session state.

```
# Enable strict multi-statement mode
strict_multi_stmt=true
```

### `strict_sp_calls`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Similar to `strict_multi_stmt`, this option allows all queries after a CALL
operation on a stored procedure to be routed to the master. This option is
disabled by default and was added in MaxScale 2.1.9.

All warnings and restrictions that apply to `strict_multi_stmt` also apply to
`strict_sp_calls`.

### `master_failure_mode`

- **Type**: enum
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `fail_instantly`, `fail_on_write`, `error_on_write`
- **Default**: `fail_instantly`

This option controls how the failure of a master server is handled. By default,
the router will close the client connection as soon as the master is lost.

The following table describes the values for this option and how they treat the
loss of a master server.

| Value        | Description|
|--------------|-----------|
|fail_instantly | When the failure of the master server is detected, the connection will be closed immediately.|
|fail_on_write | The client connection is closed if a write query is received when no master is available.|
|error_on_write | If no master is available and a write query is received, an error is returned stating that the connection is in read-only mode.|

These also apply to new sessions created after the master has failed. This means
that in `fail_on_write` or `error_on_write` mode, connections are accepted as
long as slave servers are available.

When configured with `fail_on_write` or `error_on_write`, sessions that are idle
will not be closed even if all backend connections for that session have
failed. This is done in the hopes that before the next query from the idle
session arrives, a reconnection to one of the slaves is made. However, this can
leave idle connections around unless the client application actively closes
them. To prevent this, use the
[connection_timeout](../Getting-Started/Configuration-Guide.md#connection_timeout)
parameter.

**Note:** If `master_failure_mode` is set to `error_on_write` and the connection
to the master is lost, by default, clients will not be able to execute write
queries without reconnecting to MariaDB MaxScale once a new master is
available. If [`master_reconnection`](#master_reconnection) is enabled, the
session can recover if one of the slaves is promoted as the master.

### `retry_failed_reads`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true

This option controls whether autocommit selects are retried in case of failure.
This option is enabled by default.

When a simple autocommit select is being executed outside of a transaction and
the slave server where the query is being executed fails, readwritesplit can
retry the read on a replacement server. This makes the failure of a slave
transparent to the client.

### `delayed_retry`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Retry queries over a period of time. This parameter takes a boolean value, was
added in Maxscale 2.3.0 and is disabled by default.

When this feature is enabled, a failure to route a query due to a connection
problem will not immediately result in an error. The routing of the query is
delayed until either a valid candidate server is available or the retry timeout
is reached. If a candidate server becomes available before the timeout is
reached, the query is routed normally and no connection error is returned. If no
candidates are found and the timeout is exceeded, the router returns to normal
behavior and returns an error.

When combined with the `master_reconnection` parameter, failures of writes done
outside of transactions can be hidden from the client connection. This allows a
master to be replaced while a write is in progress.

The delayed query retrying mode in readwritesplit does not do any sort of
duplicate write detection. To prevent accidental data duplication, it is highly
recommended to tune the monitor timeouts to values that produce accurate
results.

Duplicate execution of a statement can occur if the connection to the server is
lost or the server crashes but the server comes back up before the timeout for
the retrying is exceeded. At this point, if the server managed to read the
client's statement, it will be executed. For this reason, it is recommended to
only enable `delayed_retry` when the possibility of duplicate statement
execution is an acceptable risk.

### `delayed_retry_timeout`

- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 10s

The duration to wait until an error is returned to the client when
`delayed_retry` is enabled. The default value is 10 seconds.

The timeout is specified as documented
[here](../Getting-Started/Configuration-Guide.md#durations). If no explicit unit
is provided, the value is interpreted as seconds in MaxScale 2.4. In subsequent
versions a value without a unit may be rejected. Note that since the granularity
of the timeout is seconds, a timeout specified in milliseconds will be rejected,
even if the duration is longer than a second.

### `transaction_replay`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Replay interrupted transactions. This parameter was added in MaxScale 2.3.0 and
is disabled by default. Enabling this parameter enables both `delayed_retry` and
`master_reconnection` and sets `master_failure_mode` to `fail_on_write`, thereby
overriding any configured values for these parameters.

When the server where the transaction is in progress fails, readwritesplit can
migrate the transaction to a replacement server. This can completely hide the
failure of a master node without any visible effects to the client.

If no replacement node becomes available, the client connection is closed.

To control how long a transaction replay can take, use
`transaction_replay_timeout`.

Please refer to the
[Transaction Replay Limitations](#transaction-replay-limitations) section for
a more detailed explanation of what should and should not be done with
transaction replay.

### `transaction_replay_max_size`

- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 1 MiB

The limit on transaction size for transaction replay in bytes. Any transaction
that exceeds this limit will not be replayed. The default value is 1 MiB. This
limit applies at a session level which means that the total peak memory
consumption can be `transaction_replay_max_size` times the number of client
connections.

The amount of memory needed to store a particular transaction will be slightly
larger than the length in bytes of the SQL used in the transaction. If the limit
is ever exceeded, a message will be logged at the info level.

Read [the configuration guide](../Getting-Started/Configuration-Guide.md#sizes)
for more details on size type parameters in MaxScale.

### `transaction_replay_attempts`

- **Type**: integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 5

The upper limit on how many times a transaction replay is attempted before
giving up. The default value is 5.

A transaction replay failure can happen if the server where the transaction is
being replayed fails while the replay is in progress. In practice this parameter
controls how many server and network failures a single transaction replay
tolerates. If a transaction is replayed successfully, the counter for failed
attempts is reset.

### `transaction_replay_timeout`

- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 0s

The time how long transactions are attempted for. This feature is disabled by
default and was added in MaxScale 6.2.1. To explicitly disable this feature, set
the value to 0 seconds.

The timeout is
[a duration type](../Getting-Started/Configuration-Guide.md#durations)
and the value must include a unit for the duration.

When `transaction_replay_timeout` is enabled, the time a transaction replay can
take is controlled solely by this parameter. This is a more convenient and
predictable method of controlling how long a transaction replay can be attempted
before the connection is closed.

If `delayed_retry_timeout` is less than `transaction_replay_timeout`, it is set
to the same value.

By default the time how long a transaction can be retried is controlled by
`delayed_retry_timeout` and `transaction_replay_attempts`. This can result in a
maximum replay time limit of `delayed_retry_timeout` multiplied by
`transaction_replay_attempts`, by default this is 50 seconds. The minimum replay
time limit can be as low as `transaction_replay_attempts` seconds (5 seconds by
default) in cases where the connection fails after it was created. Usually this
happens due to problems like the max_connections limit being hit on the database
server or an Xpand group change being in progress.

With the introduction of `transaction_replay_timeout`, these problems are
avoided. Starting with MaxScale 6.2.1, this is the recommended method of
controlling the timeouts for transaction replay.

### `transaction_replay_retry_on_deadlock`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Enable automatic retrying of transactions that end up in a deadlock. This
parameter was added in MaxScale 2.4.6 and the feature is disabled by
default. MaxScale versions from 2.4.0 to 2.4.5 always tried to replay deadlocked
transactions.

If this feature is enabled and a transaction returns a deadlock error
(e.g. `SQLSTATE 40001: Deadlock found when trying to get lock; try restarting transaction`),
the transaction is automatically retried. If the retrying of the transaction
results in another deadlock error, it is retried until it either succeeds or a
transaction checksum error is encountered.

### `transaction_replay_retry_on_mismatch`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Retry transactions that end in checksum mismatch. This parameter was added in
MaxScale 6.2.1 is disabled by default.

When enabled, any replayed transactions that end with a checksum mismatch are
retried until they either succeeds or one of the transaction replay limits is
reached (`delayed_retry_timeout`, `transaction_replay_timeout` or
`transaction_replay_attempts`).

### `transaction_replay_checksum`

- **Type**: enum
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `full`, `result_only`, `no_insert_id`
- **Default**: `full`

Selects which transaction checksum method is used to verify the result of the
replayed transaction.

Note that only `transaction_replay_checksum=full` is guaranteed to retain the
consistency of the replayed transaction.

Possible values are:

* `full` (default)

  * All responses from the server are included in the checksum. This retains the
    full consistency guarantee of the replayed transaction as it must match
    exactly the one that was already returned to the client.

* `result_only`

  * Only resultsets and errors are included in the checksum. OK packets
    (i.e. successful queries that do not return results) are ignored. This mode
    is intended to be used in cases where the extra information (auto-generated
    ID, warnings etc.) returned in the OK packet is not used by the
    application.

    This mode is safe to use only if the auto-generated ID is not actually used
    by any following queries. An example of such behavior would be a transaction
    that ends with an `INSERT` into a table with an `AUTO_INCREMENT` field.

* `no_insert_id`

  * The same as `result_only` but results from queries that use
    `LAST_INSERT_ID()` are also ignored. This mode is safe to use only if the
    result of the query is not used by any subsequent statement in the
    transaction.

### `optimistic_trx`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Enable optimistic transaction execution. This parameter controls whether normal
transactions (i.e. `START TRANSACTION` or `BEGIN`) are load balanced across
slaves. This feature is disabled by default and enabling it implicitly enables
`transaction_replay`, `delayed_retry` and `master_reconnection` parameters.

When this mode is enabled, all transactions are first attempted on slave
servers. If the transaction contains no statements that modify data, it is
completed on the slave. If the transaction contains statements that modify data,
it is rolled back on the slave server and restarted on the master. The rollback
is initiated the moment a data modifying statement is intercepted by
readwritesplit so only read-only statements are executed on slave servers.

As with `transaction_replay` and transactions that are replayed, if the results
returned by the master server are not identical to the ones returned by the
slave up to the point where the first data modifying statement was executed, the
connection is closed. If the execution of ROLLBACK statement on the slave fails,
the connection to that slave is closed.

All limitations that apply to `transaction_replay` also apply to
`optimistic_trx`.

### `causal_reads`

- **Type**: enum
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `none`, `local`, `global`, `fast`, `universal`
- **Default**: `none`

Enable causal reads. This parameter is disabled by default and was introduced in
MaxScale 2.3.0.

If a client connection modifies the database and `causal_reads` is enabled, any
subsequent reads performed on slave servers will be done in a manner that
prevents replication lag from affecting the results. This only applies to the
modifications done by the client itself.

**Note:** This feature requires MariaDB 10.2.16 or newer to function. In
  addition to this, the `session_track_system_variables` parameter must include
  `last_gtid` in its list of tracked system variables.

The possible values for this parameter are:

* `none` (default)

  * Read causality is disabled.

* `local`

  * Writes are locally visible. Writes are guaranteed to be visible only to the
    connection that does it. Unrelated modifications done by other connections
    are not visible. This mode improves read scalability at the cost of latency
    and reduces the overall load placed on the master server without breaking
    causality guarantees.

* `global`

  * Writes are globally visible. If one connection writes a value, all
    connections to the same service will see it. In general this mode is slower
    than the `local` mode due to the extra synchronization it has to do. This
    guarantees global happens-before ordering of reads when all transactions are
    inside a single GTID domain.This mode gives similar benefits as the `local`
    mode in that it improves read scalability at the cost of latency.

    With MaxScale versions 2.5.14 and older, multi-domain use of causal_reads
    could cause non-causal reads to occur. Starting with MaxScale 2.5.15, this
    was fixed and all the GTID coordinates are passed alongside all requests
    which makes multi-domain GTIDs safe to use. However, this does mean that the
    GTID coordinates will never be reset: if replication is reset and and GTID
    coordinates go "backwards", readwritesplit will not consider these as being
    newer than the ones already stored. To reset the stored GTID coordinates in
    readwritesplit, MaxScale must be restarted.

* `fast`

  * This mode is similar to the `local` mode where it will only affect the
    connection that does the write but where the `local` mode waits for a slave
    server to catch up, the `fast` mode will only use servers that are known to
    have replicated the write. This means that if no slave has replicated the
    write, the master where the write was done will be used. The value of
    `causal_reads_timeout` is ignored in this mode. Currently the replication
    state is only updated by the mariadbmon monitor whenever the servers are
    monitored. This means that a smaller `monitor_interval` provides faster
    replication state updates and possibly better overall usage of servers.

    This mode is the inverse of the `local` mode in the sense that it improves
    read latency at the cost of read scalability while still retaining the
    causality guarantees for reads. This functionality can also be considered an
    improved version of the functionality that the
    [CCRFilter](../Filters/CCRFilter.md) module provides.

* `universal`

  * The universal mode guarantees that all SELECT statements always see the
    latest observable transaction state on a database cluster. The basis of this
    is the `@@gtid_current_pos` variable which is read from the current master
    server before each read. This guarantees that if a transaction was visible
    at the time the read is received by readwritesplit, the transaction is
    guaranteed to be complete on the slave server where the read is done.

    This mode is the most consistent of all the modes. It provides consistency
    regardless of where a write originated from but it comes at the cost of
    increased latency. For every read, a round trip to the current master server
    is done. This means that the latency of any given SELECT statement increases
    by roughly twice the network latency between MaxScale and the database
    cluster. In addition, an extra SELECT statement is always executed on the
    master which places some load on the server.

Before MaxScale 2.5.0, the `causal_reads` parameter was a boolean
parameter. False values translated to `none` and true values translated to
`local`. The use of boolean parameters is deprecated but still accepted in
MaxScale 2.5.0.

#### Implementation of `causal_reads`

This feature is based on the `MASTER_GTID_WAIT` function and the tracking of
server-side status variables. By tracking the latest GTID that each statement
generates, readwritesplit can then perform a synchronization operation with the
help of the `MASTER_GTID_WAIT` function.

If the slave has not caught up to the master within the configured time, it will
be retried on the master. In MaxScale 2.3.0 an error was returned to the client
when the slave timed out.

The exception to this rule is the `fast` mode which does not do any
synchronization at all. This can be done as any reads that would go to
out-of-date servers will be re-routed to the current master.

##### Normal SQL

A practical example can be given by the following set of SQL commands executed
with `autocommit=1`.

```sql
INSERT INTO test.t1 (id) VALUES (1);
SELECT * FROM test.t1 WHERE id = 1;
```

As the statements are not executed inside a transaction, from the load balancer's
point of view, the latter statement can be routed to a slave server. The problem
with this is that if the value that was inserted on the master has not yet
replicated to the server where the SELECT statement is being performed, it can
appear as if the value we just inserted is not there.

By prefixing these types of SELECT statements with a command that guarantees
consistent results for the reads, read scalability can be improved without
sacrificing consistency.

The set of example SQL above will be translated by MaxScale into the following
statements.

```sql
INSERT INTO test.t1 (id) VALUES (1);
SET @maxscale_secret_variable=(
    SELECT CASE
           WHEN MASTER_GTID_WAIT('0-3000-8', 10) = 0 THEN 1
           ELSE (SELECT 1 FROM INFORMATION_SCHEMA.ENGINES)
    END);
SELECT * FROM test.t1 WHERE id = 1;
```

The `SET` command will synchronize the slave to a certain logical point in
the replication stream (see
[MASTER_GTID_WAIT](https://mariadb.com/kb/en/library/master_gtid_wait/)
for more details).

##### Prepared Statements

Binary protocol prepared statements are handled in a different manner. Instead
of adding the synchronization SQL into the original SQL query, it is sent as a
separate packet before the prepared statement is executed.

We'll use the same example SQL but use a binary protocol prepared statement for
the SELECT:

```
COM_QUERY:         INSERT INTO test.t1 (id) VALUES (1);
COM_STMT_PREPARE:  SELECT * FROM test.t1 WHERE id = ?;
COM_STMT_EXECUTE:  ? = 123
```

The SQL that MaxScale executes will be the following:

```
COM_QUERY:         INSERT INTO test.t1 (id) VALUES (1);
COM_STMT_PREPARE:  SELECT * FROM test.t1 WHERE id = ?;
COM_QUERY:         IF (MASTER_GTID_WAIT('0-3000-8', 10) <> 0) THEN KILL (SELECT CONNECTION_ID()); END IF
COM_STMT_EXECUTE:  ? = 123
```

Both the synchronization query and the execution of the prepared statement are
sent at the same time. This is done to remove the need to wait for the result of
the synchronization query before routing the execution of the prepared
statement. This keeps the performance of causal_reads for prepared statements
the same as it is for normal SQL queries.

As a result of this, each time the the synchronization query times out, the
connection will be killed by the `KILL` statement and readwritesplit will retry
the query on the master. This is done to prevent the execution of the prepared
statement that follows the synchronization query from being processed by the
MariaDB server.

It is recommend that the session command history is enabled whenever prepared
statements are used with `causal_reads`. This allows new connections to be
created whenever a causal read times out.

Starting with MaxScale 2.5.17, a failed causal read inside of a read-only
transaction started with `START TRANSACTION READ ONLY` will return the following
error:

```
Error:    1792
SQLSTATE: 25006
Message:  Causal read timed out while in a read-only transaction, cannot retry command.
```

Older versions of MaxScale attempted to retry the command on the current master
server which would cause the connection to be closed and a warning to be logged.

#### Limitations of Causal Reads

- This feature does not work with Galera or any other non-standard
  replication mechanisms. As Galera does not update the `gtid_slave_pos`
  variable when events are replicated via the Galera library, the
  [`MASTER_GTID_WAIT`](https://mariadb.com/kb/en/library/master_gtid_wait/)
  function used by MaxScale to synchronize reads will wait until the
  timeout. With Galera this is not a serious issue as it, by nature, is a
  mostly-synchronous replication mechanism.

- If the combination of the original SQL statement and the modifications
  added to it by readwritesplit exceed the maximum packet size (16777213 bytes),
  the causal read will not be attempted and a non-causal read is done instead.
  This applies only to text protocol queries as the binary protocol queries use
  a different synchronization mechanism.

### `causal_reads_timeout`

- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 10s

The timeout for the slave synchronization done by `causal_reads`. The
default value is 10 seconds.

The timeout is specified as documented
[here](../Getting-Started/Configuration-Guide.md#durations). If no explicit unit
is provided, the value is interpreted as seconds in MaxScale 2.4. In subsequent
versions a value without a unit may be rejected. Note that since the granularity
of the timeout is seconds, a timeout specified in milliseconds will be rejected,
even if the duration is longer than a second.

### `lazy_connect`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Lazy connection creation causes connections to backend servers to be opened only
when they are needed. This reduces the load that is placed on the backend
servers when the client connections are short. This parameter is a boolean type
and is disabled by default.

By default readwritesplit opens as many connections as it can when the session
is first opened. This makes the execution of the first query faster when all
available connections are already created. When `lazy_connect` is enabled, this
initial connection creation is skipped. If the client executes only read
queries, no connection to the master is made. If only write queries are made,
only the master connection is used.

### `reuse_prepared_statements`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Reuse identical prepared statements inside the same client connection. This is a
boolean parameter and is disabled by default. This feature only applies to
binary protocol prepared statements.

When this parameter is enabled and the connection prepares an identical prepared
statement multiple times, instead of preparing it on the server the existing
prepared statement handle is reused. This also means that whenever prepared
statements are closed by the client, they will be left open by readwritesplit.

Enabling this feature will increase memory usage of a session. The amount of
memory stored per prepared statement is proportional to the length of the
prepared SQL statement and the number of parameters the statement has.

## Router Diagnostics

The `router_diagnostics` output for a readwritesplit service contains the
following fields.

* `queries`: Number of queries executed through this service.
* `route_master`: Number of writes routed to master.
* `route_slave`: Number of reads routed to slaves.
* `route_all`: Number of session commands routed to all servers.
* `rw_transactions`: Number of explicit read-write transactions.
* `ro_transactions`: Number of explicit read-only transactions.
* `replayed_transactions`: Number of replayed transactions.

* `server_query_statistics`: Statistics for each configured and used server consisting of the following fields.
  * `id`: Name of the server
  * `total`: Total number of queries.
  * `read`: Total number of reads.
  * `write`: Total number of writes.
  * `avg_sess_duration`: Average duration of a client session to this server.
  * `avg_sess_active_pct`: Average percentage of time client sessions were active. 0% means connections were opened but never used.
  * `avg_selects_per_session`: Average number of selects per session.

## Server Ranks

The general rule with server ranks is that primary servers will be used before
secondary servers. Readwritesplit is an exception to this rule. The following
rules govern how readwritesplit behaves with servers that have different ranks.

* Sessions will use the current master server as long as possible. This means
  that sessions with a secondary master will not use the primary master as long
  as the secondary master is available.

* All slave connections will use the same rank as the master connection. Any
  stale connections with a different rank than the master will be discarded.

* If no master connection is available and `master_reconnection` is enabled, a
  connection to the best master is created. If the new master has a different
  priority than existing connections have, the connections with a different rank
  will be discarded.

* If open connections exist, these will be used for routing. This means that if
  the master is lost but the session still has slave servers with the same rank,
  they will remain in use.

* If no open connections exist, the servers with the best rank will used.


## Routing hints

The readwritesplit router supports routing hints. For a detailed guide on hint
syntax and functionality, please read [this](../Reference/Hint-Syntax.md)
document.

**Note**: Routing hints will always have the highest priority when a routing
decision is made. This means that it is possible to cause inconsistencies in
the session state and the actual data in the database by adding routing hints
to DDL/DML statements which are then directed to slave servers. Only use routing
hints when you are sure that they can cause no harm.

An exception to this rule is `transaction_replay`: when it is enabled, all
routing hints inside transaction are ignored. This is done to prevent changes
done inside a re-playable transaction from affecting servers outside of the
transaction. This behavior was added in MaxScale 6.1.4. Older versions allowed
routing hints to override the transaction logic.

## Examples

Examples of the readwritesplit router in use can be found in the
[Tutorials](../Tutorials) folder.

## Readwritesplit routing decisions

Here is a small explanation which shows what kinds of queries are routed to
which type of server.

### Routing to Master

Routing to master is important for data consistency and because majority of
writes are written to binlog and thus become replicated to slaves.

The following operations are routed to master:

* DML statements (`INSERT`, `UPDATE`, `DELETE` etc.)
* DDL statements (`DROP`, `CREATE`, `ALTER` etc.)
* All statements within an open read-write transaction
* Stored procedure calls
* User-defined function calls
* Statements that use `LAST_INSERT_ID()`

In addition to these, if the **readwritesplit** service is configured with the
`max_slave_replication_lag` parameter, and if all slaves suffer from too much
replication lag, then statements will be routed to the _Master_. (There might be
other similar configuration parameters in the future which limit the number of
statements that will be routed to slaves.)

#### Transaction Isolation Level Tracking

Use of the SERIALIZABLE transaction isolation level with readwritesplit is not
recommended as it somewhat goes against the goals of load balancing.

If either `session_track_transaction_info=CHARACTERISTICS` or
`session_track_system_variables=tx_isolation` is configured for the MariaDB
server, readwritesplit will track the transaction isolation level and lock the
session to the master when the isolation level is set to serializable. This
retains the correctness of the isolation level which can otherwise cause
problems. Once a session is locked to the master, it will not be unlocked. To
reinstate the normal routing behavior, a new connection must be created.

For example, if transaction isolation level tracking cannot be done and an
autocommit SELECT is routed to a slave, it no longer behaves in a serializable
manner. This can also have an effect on the replication in the slave server.

### Routing to Slaves

The ability to route some statements to slaves is important because it also
decreases the load targeted to master. Moreover, it is possible to have multiple
slaves to share the load in contrast to single master.

Queries which can be routed to slaves must be auto committed and belong to one
of the following group:

* Read-only statements (i.e. `SELECT`) that only use read-only built-in functions
* All statements within an explicit read-only transaction (`START TRANSACTION READ ONLY`)
* `SHOW` statements except `SHOW MASTER STATUS`

The list of supported built-in functions can be found
[here](../../query_classifier/qc_sqlite/builtin_functions.c).

### Routing to every session backend

A third class of statements includes those which modify session data, such as
session system variables, user-defined variables, the default database, etc. We
call them session commands, and they must be replicated as they affect the
future results of read and write operations. They must be executed on all
servers that could execute statements on behalf of this client.

Session commands include for example:

* Commands that modify the session state (`SET`, `USE`, `CHANGE USER`)
* Text protocol `PREPARE` statements
* Binary protocol prepared statements
* Other miscellaneous commands (COM_QUIT, COM_PING etc.)

**NOTE**: if variable assignment is embedded in a write statement it is routed
to _Master_ only. For example, `INSERT INTO t1 values(@myvar:=5, 7)` would be
routed to _Master_ only.

The router stores all of the executed session commands so that in case of a
slave failure, a replacement slave can be chosen and the session command history
can be repeated on that new slave. This means that the router stores each
executed session command for the duration of the session. Applications that use
long-running sessions might cause MariaDB MaxScale to consume a growing amount
of memory unless the sessions are closed. This can be solved by adjusting the
value of `max_sescmd_history`.

### Routing to previous target

In the following cases, a query is routed to the same server where the previous
query was executed. If no previous target is found, the query is routed to the
current master.

* If a query uses the `FOUND_ROWS()` function, it will be routed to the server
  where the last query was executed. This is done with the assumption that a
  query with `SQL_CALC_FOUND_ROWS` was previously executed.

* COM_STMT_FETCH_ROWS will always be routed to the same server where the
  COM_STMT_EXECUTE was routed.

## Limitations

Read queries are routed to the master server in the following situations:

* Query is executed inside an open read-write transaction
* Statement includes a stored procedure or an UDF call
* If there are multiple statements inside one query e.g.
  `INSERT INTO ... ; SELECT LAST_INSERT_ID();`

### Prepares Statement Limitations

If a prepared statement targets a temporary table on the master, the slave
servers will fail to execute it. This will cause all slave connections to be
closed (MXS-1816).

### Transaction Replay Limitations

If the results from the replacement server are not identical when the
transaction is replayed, the client connection is closed. This means that any
transaction with a server specific result (e.g. `NOW()`, `@@server_id`) cannot
be replayed successfully but it will still be attempted.

If a transaction reads data before updating it, the rows should be locked by
using `SELECT ... FOR UPDATE`. This will prevent overlapping transactions when
multiple transactions are being replayed that modify the same set of rows.

If the connection to the server where the transaction is being executed is
lost when the final `COMMIT` is being executed, it is impossible to know
whether the transaction was successfully committed. This means that there
is a possibility for duplicate transaction execution which can result in
data duplication in certain cases. Data duplication can happen if the
transaction consists of the following statement types:

* INSERT of rows into a table that does not have an auto-increment primary key
* A "blind update" of one or more rows e.g. `UPDATE t SET c = c + 1 WHERE id = 123`
* A "blind delete" e.g. `DELETE FROM t LIMIT 100`

This is not an exhaustive list and any operations that do not check the row
contents before performing the operation on them might face this problem.

In all cases the problem of duplicate transaction execution can be avoided by
including a `SELECT ... FOR UPDATE` in the statement. This will guarantee that
in the case that the transaction fails when it is being committed, the row is
only modified if it matches the expected contents.

Similarly, a connection loss during `COMMIT` can also result in transaction
replay failure. This happens due to the same reason as duplicate transaction
execution but the retried transaction will not be committed. This can be
considered a success case as the transaction replay detected that the results of
the two transactions are different. In these cases readwritesplit will abort the
transaction and close the client connection.

Any changes to the session state (e.g. autocommit state, SQL mode) done inside a
transaction will remain in effect even if the connection to the server where the
transaction is being executed fails. When readwritesplit creates a new
connection to a server to replay the transaction, it will first restore the
session state by executing all session commands that were executed. This means
that if the session state is changed mid-transaction in a way that affects the
results, transaction replay will fail.

The following partial transaction demonstrates the problem by using
[`SQL_MODE`](https://mariadb.com/kb/en/library/sql-mode/) inside a transaction.

```
SET SQL_MODE='';            -- A session command
BEGIN;
SELECT "hello world";       -- Returns the string "hello world"
SET SQL_MODE='ANSI_QUOTES'; -- A session command
SELECT 'hello world';       -- Returns the string "hello world"
```

If this transaction has to be replayed the actual SQL that gets executed is the
following.

```
SET SQL_MODE='';            -- Replayed session command
SET SQL_MODE='ANSI_QUOTES'; -- Replayed session command
BEGIN;
SELECT "hello world";       -- Returns an error
SELECT 'hello world';       -- Returns the string "hello world"
```

First the session state is restored by executing all commands that changed the
state after which the actual transaction is replayed. Due to the fact that the
SQL_MODE was changed mid-transaction, one of the queries will now return an
error instead of the result we expected leading to a transaction replay failure.

### Legacy Configuration

In older versions of MaxScale, routers were configured via the _router_options_
parameter. This functionality was deprecated in 2.2 and was removed in 2.3.

### JDBC Batched Statements

Readwritesplit does not support pipelining of JDBC batched statements. This is
caused by the fact that readwritesplit executes the statements one at a time to
track the state of the response.

#### Limitations in multi-statement handling

When a multi-statement query is executed through the readwritesplit router, it
will always be routed to the master. See
[`strict_multi_stmt`](../Routers/ReadWriteSplit.md#strict_multi_stmt) for more
details.

If the multi-statement query creates a temporary table, it will not be
detected and reads to this table can be routed to slave servers. To
prevent this, always execute the temporary table creation as an individual
statement.

#### Limitations in client session handling

Some of the queries that a client sends are routed to all backends instead of
just to one. These queries include `USE <db name>` and `SET autocommit=0`, among
many others. Readwritesplit sends a copy of these queries to each backend server
and forwards the master's reply to the client. Below is a list of MySQL commands
which are classified as session commands.

```
COM_INIT_DB (USE <db name> creates this)
COM_CHANGE_USER
COM_STMT_CLOSE
COM_STMT_SEND_LONG_DATA
COM_STMT_RESET
COM_STMT_PREPARE
COM_QUIT (no response, session is closed)
COM_REFRESH
COM_DEBUG
COM_PING
SQLCOM_CHANGE_DB (USE ... statements)
SQLCOM_DEALLOCATE_PREPARE
SQLCOM_PREPARE
SQLCOM_SET_OPTION
SELECT ..INTO variable|OUTFILE|DUMPFILE
SET autocommit=1|0
```

Prior to MaxScale 2.3.0, session commands that were 2²⁴ - 1 bytes or longer were
not supported and caused the session to be closed.

There is a possibility for misbehavior. If `USE mytable` is executed in one of
the slaves and fails, it may be due to replication lag rather than the database
not existing. Thus, the same command may produce different result in different
backend servers. The slaves which fail to execute a session command will be
dropped from the active list of slaves for this session to guarantee a
consistent session state across all the servers used by the session. In
addition, the server will not be used again for routing for the duration of the
session.

The above-mentioned behavior for user variables can be partially controlled with
the configuration parameter `use_sql_variables_in`:

```
use_sql_variables_in=[master|all] (default: all)
```

**WARNING**

If a SELECT query modifies a user variable when the `use_sql_variables_in`
parameter is set to `all`, it will not be routed and the client will receive an
error. A log message is written into the log further explaining the reason for
the error. Here is an example use of a SELECT query which modifies a user
variable and how MariaDB MaxScale responds to it.

```
MySQL [(none)]> set @id=1;
Query OK, 0 rows affected (0.00 sec)

MySQL [(none)]> SELECT @id := @id + 1 FROM test.t1;
ERROR 1064 (42000): Routing query to backend failed. See the error log for further details.
```

Allow user variable modification in SELECT queries by setting
`use_sql_variables_in=master`. This will route all queries that use user
variables to the master.
