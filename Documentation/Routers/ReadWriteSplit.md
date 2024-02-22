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

The router is designed to be used with a traditional Primary-Replica replication
cluster. It automatically detects changes in the primary server and will use the
current primary server of the cluster. With a Galera cluster, one can achieve a
resilient setup and easy primary failover by using one of the Galera nodes as a
Write-Primary node, where all write queries are routed, and spreading the read
load over all the nodes.

## Interaction with servers in `Maintenance` and `Draining` state

When a server that readwritesplit uses is put into maintenance mode, any ongoing
requests are allowed to finish before the connection is closed. If the server
that is put into maintenance mode is a primary, open transaction are allowed to
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

`max_slave_connections` sets the maximum number of replicas a router session uses
at any moment. The default is to use at most 255 replica connections per client
connection. In older versions the default was to use all available replicas with
no limit.

For MaxScale 2.5.12 and newer, the minimum value is 0.

For MaxScale versions 2.5.11 and older, the minimum value is 1. These versions
suffer from a bug ([MXS-3536](https://jira.mariadb.org/browse/MXS-3536)) that
causes the parameter to accept any values but only function when a value greater
than one was given.

Starting with MaxScale 2.5.0, the use of percentage values in
`max_slave_connections` is deprecated. The support for percentages will be
removed in a future release.

For example, if you have configured MaxScale with one primary and three replicas
and set `max_slave_connections=2`, for each client connection a connection to
the primary and two replica connections would be opened. The read query load
balancing is then done between these two replicas and writes are sent to the
primary.

By tuning this parameter, you can control how dynamic the load balancing is at
the cost of extra created connections. With a lower value of
`max_slave_connections`, less connections per session are created and the set of
possible replica servers is smaller. With a higher value in
`max_slave_connections`, more connections are created which requires more
resources but load balancing will almost always give the best single query
response time and performance. Longer sessions are less affected by a high
`max_slave_connections` as the relative cost of opening a connection is lower.

### `slave_connections`

- **Type**: integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 255

This parameter controls how many replica connections each new session starts
with. The default value is 255 which is the same as the default value of
`max_slave_connections`.

In contrast to `max_slave_connections`, `slave_connections` serves as a
soft limit on how many replica connections are created. The number of replica
connections can exceed `slave_connections` if the load balancing algorithm
finds an unconnected replica server better than all other replicas.

Setting this parameter to 1 allows faster connection creation and improved
resource usage due to the smaller amount of initial backend
connections. It is recommended to use `slave_connections=1` when the
lifetime of the client connections is short.

### `max_replication_lag`

- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 0s

**NOTE** Up until 23.02, this parameter was called `max_slave_replication_lag`,
which has been deprecated but still works as an alias for `max_replication_lag`.

Specify how many seconds a replica is allowed to be behind the primary. The lag of
a replica must be less than the configured value in order for it to be used for
routing. If set to `0s` (the default value), the feature is disabled.

The replica lag must be less than `max_replication_lag`. This means that it
is possible to define, with `max_replication_lag=1s`, that all replicas must
be up to date in order for them to be used for routing.

Note that this feature does not guarantee that writes done on the primary are
visible for reads done on the replica. This is mainly due to the method of
replication lag measurement. For a feature that guarantees this, refer to
[`causal_reads`](#causal_reads).

The lag is specified as documented
[here](../Getting-Started/Configuration-Guide.md#durations). Note that since
the granularity of the lag is seconds, a lag specified in milliseconds will be
rejected, even if the duration is longer than a second.

The Readwritesplit-router does not detect the replication lag itself. A monitor
such as the MariaDB-monitor for a Primary-Replica cluster is required. This option
only affects Primary-Replica clusters. Galera clusters do not have a concept of
replica lag even if the application of write sets might have lag. When a server is
disqualified from routing because of replication lag, a warning is logged. Similarly,
when the server has caught up enough to be a valid routing target, another warning
is logged. These messages are only logged when a query is being routed and the
replication state changes.

### `use_sql_variables_in`

- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `master`, `all`
- **Default**: `all`

This parameter controls how `SELECT` statements that use SQL user variables are
handled. Here is an example of such a query that uses it to return an increasing
row number for a resultset:

```sql
SET @rownum := 0;
SELECT @rownum := @rownum + 1 AS rownum, user, host FROM mysql.user;
```

By default MaxScale will route both the `SET` and `SELECT` statements to all
nodes. Any future reads of the user variables can also be performed on any node.

The possible values for this parameter are:

* `all` (default)

  * Modifications to user variables inside `SELECT` statements as well as reads
    of user variables are routed to all servers.

    Versions before MaxScale 22.08 returned an error if a user variable was
    modified inside of a `SELECT` statement when `use_sql_variables_in=all` was
    used. MaxScale 22.08 will instead route the query to all servers and discard
    the extra results.

* `master`

  * Modifications to user variables inside `SELECT` statements as well as reads
    of user variables are routed to the primary server. This forces more of the
    traffic onto the primary server but it reduces the amount of data that is
    discarded for any `SELECT` statement that also modifies a user
    variable. With this mode, the state of user variables is not deterministic
    if they are modified inside of a `SELECT` statement. `SET` statements that
    modify user variables are still routed to all servers.

DML statements, such as `INSERT`, `UPDATE` or `DELETE`, that modify SQL user
variables are still treated as writes and are only routed to the primary
server. For example, after the following query the value of `@myid` is no longer
the same on all servers and the `SELECT` statement can return different values
depending where it ends up being executed:

```sql
SET @myid := 0;
INSERT INTO test.t1 VALUES (@myid := @myid + 1);
SELECT @myid; -- Might return 1 or 0
```

### `master_reconnection`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true (>= MaxScale 24.02), false(<= MaxScale 23.08)

Allow the primary server to change mid-session. This feature requires that
`disable_sescmd_history` is not used.

Starting with MaxScale 24.02, if `disable_sescmd_history` is enabled,
`master_reconnection` will be automatically disabled.

When a readwritesplit session starts, it will pick a primary server as the
current primary server of that session. When `master_reconnection` is disabled,
when this primary server is lost or changes to another server, the connection
will be closed.

When `master_reconnection` is enabled, readwritesplit can sometimes recover a
lost connection to the primary server. This largely depends on the value of
`master_failure_mode`.

With `master_failure_mode=fail_instantly`, the primary server is only allowed to
change to another server. This change must happen without a loss of the primary
server.

With `master_failure_mode=fail_on_write`, the loss of the primary server is no
longer a fatal error: if a replacement primary server appears before any write
queries are received, readwritesplit will transparently reconnect to the new
primary server.

In both cases the change in the primary server can only take place if
`prune_sescmd_history` is enabled or `max_sescmd_history` has not yet
been exceeded and the session does not have an open transaction.

The recommended configuration is to use `master_reconnection=true` and
`master_failure_mode=fail_on_write`. This provides improved fault tolerance
without any risk to the consistency of the database.

### `slave_selection_criteria`

- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `least_current_operations`, `adaptive_routing`, `least_behind_master`, `least_router_connections`, `least_global_connections`
- **Default**: `least_current_operations`

This option controls how the readwritesplit router chooses the replicas it
connects to and how the load balancing is done. The default behavior is to route
read queries to the replica server with the lowest amount of ongoing queries i.e.
`least_current_operations`.

The option syntax:

```
slave_selection_criteria=<criteria>
```

Where `<criteria>` is one of the following values.

* `least_current_operations` (default), the replica with least active operations
* `adaptive_routing`, based on server average response times.
* `least_behind_master`, the replica with smallest replication lag
* `least_global_connections`, the replica with least connections from MariaDB MaxScale
* `least_router_connections`, the replica with least connections from this service

`least_current_operations` uses the current number of active operations
(i.e. SQL queries) as the load balancing metric and it optimizes for maximal
query throughput. Each query gets routed to the server with the least active
operations which results in faster servers processing more traffic.

`adaptive_routing` uses the server response time and current estimated server
load as the load balancing metric. The server that is estimated to finish an
additional query first is chosen. A modified average response time for each
server is continuously updated to allow slow servers at least some traffic and
quickly react to changes in server load conditions. This selection criteria is
designed for heterogeneous clusters: servers of differing hardware, differing
network distances, or when other loads are running on the servers (including a
backup). If the servers are queried by other clients than MaxScale, the load
caused by them is indirectly taken into account.

`least_behind_master` uses the measured replication lag as the load balancing
metric. This means that servers that are more up-to-date are favored which
increases the likelihood of the data being read being up-to-date. However, this
is not as effective as `causal_reads` would be as there's no guarantee that
writes done by the same connection will be routed to a server that has
replicated those changes. The recommended approach is to use
`LEAST_CURRENT_OPERATIONS` or `ADAPTIVE_ROUTING` in combination with
`causal_reads`

**NOTE**: `least_global_connections` and `least_router_connections` should not
be used, they are legacy options that exist only for backwards
compatibility. Using them will result in skewed load balancing as the algorithm
uses a metric that's too coarse (number of connections) to load balance
something that's finer (individual SQL queries).

The `least_global_connections` and `least_router_connections` use the
connections from MariaDB MaxScale to the server, not the amount of connections
reported by the server itself.

Starting with MaxScale versions 2.5.29, 6.4.11, 22.08.9, 23.02.5 and 23.08.1,
lowercase versions of the values are also accepted. For example,
`slave_selection_criteria=LEAST_CURRENT_OPERATIONS` and
`slave_selection_criteria=least_current_operations` are both accepted as valid
values.

Starting with MaxScale 23.08.1, the legacy uppercase values have been
deprecated. All runtime modifications of the parameter will now be persisted in
lowercase. The uppercase values are still accepted but will be removed in a
future MaxScale release.

### `master_accept_reads`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Enables the primary server to be used for reads. This is a useful option to
enable if you are using a small number of servers and wish to use the primary
for reads as well and the load on it does not reduce the write throughput of the
cluster.

By default, no reads are sent to the primary as long as there is a valid replica
server available. If no replicas are available, reads are sent to the primary
regardless of the value of `master_accept_reads`.

```
# Use the primary for reads
master_accept_reads=true
```

### `strict_multi_stmt`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

When a client executes a multi-statement query, it will be treated as if it were
a DML statement and routed to the primary. If the option is enabled, all queries
after a multi-statement query will be routed to the primary to guarantee a
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
operation on a stored procedure to be routed to the primary.

All warnings and restrictions that apply to `strict_multi_stmt` also apply to
`strict_sp_calls`.

### `strict_tmp_tables`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true (>= MaxScale 24.02), false (<= MaxScale 23.08)

When `strict_tmp_tables` is disabled, all temporary tables are lost when a
reconnection of the primary node occurs. This means that if a reconnection to
the primary takes place, temporary tables might appear to disappear in the
middle of a connection.

When `strict_tmp_tables` is enabled, reconnections are prevented as long as a
temporary tables exist. In this case if the primary node is lost and temporary
table exist, the session is closed.  If a session creates temporary tables but
does not drop them, this behavior will effectively disable reconnections until
the session is closed.

### `master_failure_mode`

- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `fail_instantly`, `fail_on_write`, `error_on_write`
- **Default**: `fail_on_write` (MaxScale 23.08: `fail_instantly`)

This option controls how the failure of a primary server is handled.

The following table describes the values for this option and how they treat the
loss of a primary server.

| Value         | Description                                                                                                                     |
|---------------|---------------------------------------------------------------------------------------------------------------------------------|
|fail_instantly | When the failure of the primary server is detected, the connection will be closed immediately.                                  |
|fail_on_write  | The client connection is closed if a write query is received when no primary is available.                                      |
|error_on_write | If no primary is available and a write query is received, an error is returned stating that the connection is in read-only mode.|

These also apply to new sessions created after the primary has failed. This means
that in `fail_on_write` or `error_on_write` mode, connections are accepted as
long as replica servers are available.

When configured with `fail_on_write` or `error_on_write`, sessions that are idle
will not be closed even if all backend connections for that session have
failed. This is done in the hopes that before the next query from the idle
session arrives, a reconnection to one of the replicas is made. However, this can
leave idle connections around unless the client application actively closes
them. To prevent this, use the
[connection_timeout](../Getting-Started/Configuration-Guide.md#connection_timeout)
parameter.

**Note:** If `master_failure_mode` is set to `error_on_write` and the connection
to the primary is lost, by default, clients will not be able to execute write
queries without reconnecting to MariaDB MaxScale once a new primary is
available. If [`master_reconnection`](#master_reconnection) is enabled, the
session can recover if one of the replicas is promoted as the primary.

### `retry_failed_reads`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true

This option controls whether autocommit selects are retried in case of failure.

When a simple autocommit select is being executed outside of a transaction and
the replica server where the query is being executed fails, readwritesplit can
retry the read on a replacement server. This makes the failure of a replica
transparent to the client.

If a part of the result was already delivered to the client, the query will not
be retried. The retrying of queries with partially delivered results is only
possible when `transaction_replay` is enabled.

### `delayed_retry`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Retry queries over a period of time.

When this feature is enabled, a failure to route a query due to a connection
problem will not immediately result in an error. The routing of the query is
delayed until either a valid candidate server is available or the retry timeout
is reached. If a candidate server becomes available before the timeout is
reached, the query is routed normally and no connection error is returned. If no
candidates are found and the timeout is exceeded, the router returns to normal
behavior and returns an error.

When combined with the `master_reconnection` parameter, failures of writes done
outside of transactions can be hidden from the client connection. This allows a
primary to be replaced while a write is in progress.

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
`delayed_retry` is enabled.

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

Replay interrupted transactions.

Enabling this parameter enables both `delayed_retry` and `master_reconnection`
and sets `master_failure_mode` to `fail_on_write`, thereby overriding any
configured values for these parameters.

When the server where the transaction is in progress fails, readwritesplit can
migrate the transaction to a replacement server. This can completely hide the
failure of a primary node without any visible effects to the client.

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

The number of times that this limit has been exceeded is shown in
`maxctrl show service` as `trx_max_size_exceeded`.

Read [the configuration guide](../Getting-Started/Configuration-Guide.md#sizes)
for more details on size type parameters in MaxScale.

### `transaction_replay_attempts`

- **Type**: integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 5

The upper limit on how many times a transaction replay is attempted before
giving up.

A transaction replay failure can happen if the server where the transaction is
being replayed fails while the replay is in progress. In practice this parameter
controls how many server and network failures a single transaction replay
tolerates. If a transaction is replayed successfully, the counter for failed
attempts is reset.

### `transaction_replay_timeout`

- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 30s (>= MaxScale 24.02), 0s (<= MaxScale 23.08)

The time how long transactions are attempted for. To explicitly disable this
feature, set the value to 0 seconds.

The timeout is
[a duration type](../Getting-Started/Configuration-Guide.md#durations)
and the value must include a unit for the duration.

When `transaction_replay_timeout` is enabled, the time a transaction replay can
take is controlled solely by this parameter. This is a more convenient and
predictable method of controlling how long a transaction replay can be attempted
before the connection is closed.

If `delayed_retry_timeout` is less than `transaction_replay_timeout`, it is set
to the same value.

Without `transaction_replay_timeout` the time how long a transaction can be
retried is controlled by `delayed_retry_timeout` and
`transaction_replay_attempts`. This can result in a maximum replay time limit of
`delayed_retry_timeout` multiplied by `transaction_replay_attempts`, by default
this is 50 seconds. The minimum replay time limit can be as low as
`transaction_replay_attempts` seconds (5 seconds by default) in cases where the
connection fails after it was created. Usually this happens due to problems like
the max_connections limit being hit on the database server.

`transaction_replay_timeout` is the recommended method of controlling the
timeouts for transaction replay and is by default set to 30 seconds in MaxScale
24.02.

### `transaction_replay_retry_on_deadlock`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Enable automatic retrying of transactions that end up in a deadlock.

If this feature is enabled and a transaction returns a deadlock error
(e.g. `SQLSTATE 40001: Deadlock found when trying to get lock; try restarting transaction`),
the transaction is automatically retried. If the retrying of the transaction
results in another deadlock error, it is retried until it either succeeds or a
transaction checksum error is encountered.

### `transaction_replay_safe_commit`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true

If a transaction is ending and the `COMMIT` statement at the end of it is
interrupted, there is a risk of duplicating the transaction if it is
replayed. This parameter prevents the retrying of transactions that are about to
commit.

This parameter was added in MaxScale 23.08.0 and is enabled by default. The
older version of MaxScale always attempted to replay the transaction even if
there was a risk of duplicating the transaction.

If the data that is about to be modified is read before it is modified and it is
locked in an appropriate manner (e.g. with `SELECT ... FOR UPDATE` or with the
`SERIALIZABLE` isolation level), it is safe to replay a transaction that was
about to commit. This is because the checksum of the transaction will mismatch
if the original transaction ended up committing on the server. Disabling this
feature can enable more robust delivery of transactions but it requires that the
SQL is correctly formed and compatible with this behavior.

### `transaction_replay_retry_on_mismatch`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Retry transactions that end in checksum mismatch.

When enabled, any replayed transactions that end with a checksum mismatch are
retried until they either succeeds or one of the transaction replay limits is
reached (`delayed_retry_timeout`, `transaction_replay_timeout` or
`transaction_replay_attempts`).

### `transaction_replay_checksum`

- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
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
replicas. This feature is disabled by default and enabling it implicitly enables
`transaction_replay`, `delayed_retry` and `master_reconnection` parameters.

When this mode is enabled, all transactions are first attempted on replica
servers. If the transaction contains no statements that modify data, it is
completed on the replica. If the transaction contains statements that modify data,
it is rolled back on the replica server and restarted on the primary. The rollback
is initiated the moment a data modifying statement is intercepted by
readwritesplit so only read-only statements are executed on replica servers.

As with `transaction_replay` and transactions that are replayed, if the results
returned by the primary server are not identical to the ones returned by the
replica up to the point where the first data modifying statement was executed, the
connection is closed. If the execution of ROLLBACK statement on the replica fails,
the connection to that replica is closed.

All limitations that apply to `transaction_replay` also apply to
`optimistic_trx`.

### `causal_reads`

- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `none`, `local`, `global`, `fast`, `fast_global`, `universal`, `fast_universal`
- **Default**: `none`

Enable causal reads. This feature requires MariaDB 10.2.16 or newer to function.

If a client connection modifies the database and `causal_reads` is enabled, any
subsequent reads performed on replica servers will be done in a manner that
prevents replication lag from affecting the results.

The following table contains a comparison of the modes.  Read the
[implementation of causal_reads](#implementation-of-causal_reads) for more
information on what a sync consists of and why minimizing the number of them is
important.

|Mode            |Level of Causality |Latency                                                  |
|----------------|-------------------|---------------------------------------------------------|
|`local`         |Session            |Low, one sync per write.                                 |
|`fast`          |Session            |None, no sync at all.                                    |
|`global`        |Service            |Medium, one sync per read.                               |
|`fast_global`   |Service            |None, no sync at all.                                    |
|`universal`     |Cluster            |High, one sync per read plus a roundtrip to the primary. |
|`fast_universal`|Cluster            |Low, one roundtrip to the primary.                       |

The `fast`, `fast_global` and `fast_universal` modes should only be used when
low latency is more important than proper distribution of reads. These modes
should only be used when the workload is mostly read-only with only occasional
writes. If used with a mixed or a write-heavy workload, the traffic will end up
being routed almost exclusively to the primary server.

The possible values for this parameter are:

* `none` (default)

  * Read causality is disabled.

* `local`

  * Writes are locally visible. Writes are guaranteed to be visible only to the
    connection that does it. Unrelated modifications done by other connections
    are not visible. This mode improves read scalability at the cost of latency
    and reduces the overall load placed on the primary server without breaking
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
    GTID coordinates will never be reset: if replication is reset and GTID
    coordinates go "backwards", readwritesplit will not consider these as being
    newer than the ones already stored. To reset the stored GTID coordinates in
    readwritesplit, MaxScale must be restarted.

    MaxScale 6.4.11 added the new `reset-gtid` module command to
    readwritesplit. This allows the global GTID state used by
    `causal_reads=global` to be reset without having to restart MaxScale.

* `fast`

  * This mode is similar to the `local` mode where it will only affect the
    connection that does the write but where the `local` mode waits for a replica
    server to catch up, the `fast` mode will only use servers that are known to
    have replicated the write. This means that if no replica has replicated the
    write, the primary where the write was done will be used. The value of
    `causal_reads_timeout` is ignored in this mode. Currently the replication
    state is only updated by the mariadbmon monitor whenever the servers are
    monitored. This means that a smaller `monitor_interval` provides faster
    replication state updates and possibly better overall usage of servers.

    This mode is the inverse of the `local` mode in the sense that it improves
    read latency at the cost of read scalability while still retaining the
    causality guarantees for reads. This functionality can also be considered an
    improved version of the functionality that the
    [CCRFilter](../Filters/CCRFilter.md) module provides.

* `fast_global`

  * This mode is identical to the `fast` mode except that it uses the global
    GTID instead of the session local one. This is similar to how `local` and
    `global` modes differ from each other. The value of `causal_reads_timeout`
    is ignored in this mode. Currently the replication state is only updated by
    the mariadbmon monitor whenever the servers are monitored. This means that a
    smaller `monitor_interval` provides faster replication state updates and
    possibly better overall usage of servers.

* `universal`

  * The universal mode guarantees that all SELECT statements always see the
    latest observable transaction state on a database cluster. The basis of this
    is the `@@gtid_current_pos` variable which is read from the current primary
    server before each read. This guarantees that if a transaction was visible
    at the time the read is received by readwritesplit, the transaction is
    guaranteed to be complete on the replica server where the read is done.

    This mode is the most consistent of all the modes. It provides consistency
    regardless of where a write originated from but it comes at the cost of
    increased latency. For every read, a round trip to the current primary server
    is done. This means that the latency of any given SELECT statement increases
    by roughly twice the network latency between MaxScale and the database
    cluster. In addition, an extra SELECT statement is always executed on the
    primary which places some load on the server.

* `fast_universal`

  * A mix of `fast` and `universal`. This mode that guarantees that all SELECT
    statements always see the latest observable transaction state but unlike the
    `universal` mode that waits on the server to catch up, this mode behaves
    like `fast` and routes the query to the current primary if no replicas are
    available that have caught up.

    This mode provides the same consistency guarantees of `universal` with a
    constant latency overhead of one extra roundtrip. However, this also puts
    the most load on the primary node as even a moderate write load can cause
    the GTIDs of replicas to lag too far behind.

Before MaxScale 2.5.0, the `causal_reads` parameter was a boolean
parameter. False values translated to `none` and true values translated to
`local`. The use of boolean parameters is deprecated but still accepted in
MaxScale 2.5.0.

#### Implementation of `causal_reads`

This feature is based on the `MASTER_GTID_WAIT` function and the tracking of
server-side status variables. By tracking the latest GTID that each statement
generates, readwritesplit can then perform a synchronization operation with the
help of the `MASTER_GTID_WAIT` function.

If the replica has not caught up to the primary within the configured time, it will
be retried on the primary.

The exception to this rule is the `fast` mode which does not do any
synchronization at all. This can be done as any reads that would go to
out-of-date servers will be re-routed to the current primary.

##### Normal SQL

A practical example can be given by the following set of SQL commands executed
with `autocommit=1`.

```sql
INSERT INTO test.t1 (id) VALUES (1);
SELECT * FROM test.t1 WHERE id = 1;
```

As the statements are not executed inside a transaction, from the load balancer's
point of view, the latter statement can be routed to a replica server. The problem
with this is that if the value that was inserted on the primary has not yet
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

The `SET` command will synchronize the replica to a certain logical point in
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

As a result of this, each time the synchronization query times out, the
connection will be killed by the `KILL` statement and readwritesplit will retry
the query on the primary. This is done to prevent the execution of the prepared
statement that follows the synchronization query from being processed by the
MariaDB server.

It is recommend that the session command history is enabled whenever prepared
statements are used with `causal_reads`. This allows new connections to be
created whenever a causal read times out.

A failed causal read inside of a read-only transaction started with
`START TRANSACTION READ ONLY` will return the following error:

```
Error:    1792
SQLSTATE: 25006
Message:  Causal read timed out while in a read-only transaction, cannot retry command.
```

Older versions of MaxScale attempted to retry the command on the current primary
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

The timeout for the replica synchronization done by `causal_reads`.

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
servers when the client connections are short.

Normally readwritesplit opens as many connections as it can when the session is
first opened. This makes the execution of the first query faster when all
available connections are already created. When `lazy_connect` is enabled, this
initial connection creation is skipped. If the client executes only read
queries, no connection to the primary is made. If only write queries are made,
only the primary connection is used.

In MaxScale 23.08.2, if a [session command](#routing-to-every-session-backend)
is received as the first command, the default behavior is to execute it on a
replica. If [master_accept_reads](#master_accept_reads) is enabled, the query is
executed on the primary server, if one is available. In practice this means that
workloads which are mostly reads with infrequent writes should disable
`master_accept_reads` if they also use `lazy_connect`.

Older versions of MaxScale always tried to execute all session commands on the
primary node if one was available.

### `reuse_prepared_statements`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Reuse identical prepared statements inside the same client connection. This
feature only applies to binary protocol prepared statements.

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
* `route_master`: Number of writes routed to primary.
* `route_slave`: Number of reads routed to replicas.
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

* Sessions will use the current primary server as long as possible. This means
  that sessions with a secondary primary will not use the main primary as long
  as the secondary primary is available.

* All replica connections will use the same rank as the primary connection. Any
  stale connections with a different rank than the primary will be discarded.

* If no primary connection is available and `master_reconnection` is enabled, a
  connection to the best primary is created. If the new primary has a different
  priority than existing connections have, the connections with a different rank
  will be discarded.

* If open connections exist, these will be used for routing. This means that if
  the primary is lost but the session still has replica servers with the same rank,
  they will remain in use.

* If no open connections exist, the servers with the best rank will used.


## Routing hints

The readwritesplit router supports routing hints. For a detailed guide on hint
syntax and functionality, please read [this](../Reference/Hint-Syntax.md)
document.

**Note**: Routing hints will always have the highest priority when a routing
decision is made. This means that it is possible to cause inconsistencies in
the session state and the actual data in the database by adding routing hints
to DDL/DML statements which are then directed to replica servers. Only use routing
hints when you are sure that they can cause no harm.

An exception to this rule is `transaction_replay`: when it is enabled, all
routing hints inside transaction are ignored. This is done to prevent changes
done inside a re-playable transaction from affecting servers outside of the
transaction. This behavior was added in MaxScale 6.1.4. Older versions allowed
routing hints to override the transaction logic.

### Known Limitations of Routing Hints

* If a `SELECT` statement with a `maxscale route to slave` hint is received
  while autocommit is disabled, the query will be routed to a replica server. This
  causes some metadata locks to be acquired on the database in question which
  will block DDL statements on the server until either the connection is closed
  or autocommit is enabled again.

## Module Commands

The readwritesplit router implements the following module commands.

### `reset-gtid`

The command resets the global GTID state in the router. It can be used with
`causal_reads=global` to reset the state. This can be useful when the cluster is
reverted to an earlier state and the GTIDs recorded in MaxScale are no longer
valid.

The first and only argument to the command is the router name. For example, to
reset the GTID state of a readwritesplit named `My-RW-Router`, the following
MaxCtrl command should be used:

```
maxctrl call command readwritesplit reset-gtid My-RW-Router
```

## Examples

Examples of the readwritesplit router in use can be found in the
[Tutorials](../Tutorials) folder.

## Readwritesplit routing decisions

Here is a small explanation which shows what kinds of queries are routed to
which type of server.

### Routing to Primary

Routing to primary is important for data consistency and because majority of
writes are written to binlog and thus become replicated to replicas.

The following operations are routed to primary:

* DML statements (`INSERT`, `UPDATE`, `DELETE` etc.)
* DDL statements (`DROP`, `CREATE`, `ALTER` etc.)
* All statements within an open read-write transaction
* Stored procedure calls
* User-defined function calls
* Statements that use `LAST_INSERT_ID()`

In addition to these, if the **readwritesplit** service is configured with the
`max_replication_lag` parameter, and if all replicas suffer from too much
replication lag, then statements will be routed to the primary. (There might be
other similar configuration parameters in the future which limit the number of
statements that will be routed to replicas.)

#### Transaction Isolation Level Tracking

If either `session_track_transaction_info=CHARACTERISTICS` or
`session_track_system_variables=tx_isolation` is configured for the MariaDB
server, readwritesplit will track the transaction isolation level and lock the
session to the primary when the isolation level is set to serializable. This
retains the correctness of the isolation level which can otherwise cause
problems.

Starting with MaxScale 23.08, once the transaction isolation level is set to
something other than `SERIALIZABLE`, the session is no longer locked to the
primary and returns to its normal state. Older versions of MaxScale remain
locked to the primary even if the session goes out of the `SERIALIZABLE`
isolation level.


### Routing to Replicas

The ability to route some statements to replicas is important because it also
decreases the load targeted to _primary_. Moreover, it is possible to have multiple
replicas to share the load in contrast to single primary.

Queries which can be routed to replicas must be auto committed and belong to one
of the following group:

* Read-only statements (i.e. `SELECT`) that only use read-only built-in functions
* All statements within an explicit read-only transaction (`START TRANSACTION READ ONLY`)
* `SHOW` statements except `SHOW MASTER STATUS`

The list of supported built-in functions can be found
[here](https://github.com/mariadb-corporation/MaxScale/blob/23.02/query_classifier/qc_sqlite/builtin_functions.cc).

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
to _primary_ only. For example, `INSERT INTO t1 values(@myvar:=5, 7)` would be
routed to _primary_ only.

The router stores all of the executed session commands so that in case of a
replica failure, a replacement replica can be chosen and the session command history
can be repeated on that new replica. This means that the router stores each
executed session command for the duration of the session. Applications that use
long-running sessions might cause MariaDB MaxScale to consume a growing amount
of memory unless the sessions are closed. This can be solved by adjusting the
value of `max_sescmd_history`.

### Routing to previous target

In the following cases, a query is routed to the same server where the previous
query was executed. If no previous target is found, the query is routed to the
current primary.

* If a query uses the `FOUND_ROWS()` function, it will be routed to the server
  where the last query was executed. This is done with the assumption that a
  query with `SQL_CALC_FOUND_ROWS` was previously executed.

* COM_STMT_FETCH_ROWS will always be routed to the same server where the
  COM_STMT_EXECUTE was routed.

## Limitations

Read queries are routed to the primary server in the following situations:

* Query is executed inside an open read-write transaction
* Statement includes a stored procedure or an UDF call
* If there are multiple statements inside one query e.g.
  `INSERT INTO ... ; SELECT LAST_INSERT_ID();`

### Prepared Statement Limitations

If a prepared statement targets a temporary table on the primary, the replica
servers will fail to execute it. This will cause all replica connections to be
closed (MXS-1816).

### Transaction Replay Limitations

When transaction replay is enabled, readwritesplit calculates a checksum of the
server responses for each transaction. This is used to determine whether a
replayed transaction was identical to the original transaction. Starting with
MaxScale 23.08, a 128-bit xxHash checksum is stored for each statement that is
in the transaction. Older versions of MaxScale used a single 160-bit SHA1
checksum for the whole transaction.

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
data duplication in certain cases.

In MaxScale 23.08, the `transaction_replay_safe_commit` variable controls
whether a replay is attempted or not whenever a `COMMIT` is interrupted. By
default the transaction will not be replayed. Older versions of MaxScale always
replayed the transaction.

Data duplication can happen if the transaction consists of the following
statement types:

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

Statements that result in an implicit commit do not reset the transaction when
transaction_replay is enabled. This means that if the transaction is replayed,
the transaction will be committed twice due to the implicit commit being
present. The exception to this are the transaction management statements such as
`BEGIN` and `START TRANSACTION`: they are detected and will cause the
transaction to be correctly reset.

In older versions of MaxScale, if a connection to a server is lost while a
statement is being executed and the result was partially delivered to the
client, readwritesplit would immediately close the session without attempting to
replay the failing statement. Starting with MaxScale 23.08, this limitation no
longer applies if the statement was done inside of a transaction and
`transaction_replay` is enabled
([MXS-4549](https://jira.mariadb.org/browse/MXS-4549)).

If the connection to the server where a transaction is being executed is lost
while a `ROLLBACK` is being executed, readwritesplit will still attempt to
replay the transaction in the hopes that the real response can be delivered to
the client. However, this does mean that it is possible that a rolled back
transaction which gets replayed ends up with a conflict and is reported as a
replay failure when in reality a rolled back transaction could be safely
ignored.

#### Limitations in Session State Modifications

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

#### Limitations in Service-to-Service Routing

In a service-to-service configuration (i.e. a service using another service in
its `targets` list ), if the topmost service starts a transaction, all
lower-level readwritesplit services will also behave as if a transaction is
open. If a connection to a backend database fails during this, it can result in
unnecessary transaction replays which in turn can end up with checksum
conflicts. The recommended approach is to not use any commands inside a
transaction that would be routed to more than one node.

#### Limitations in multi-statement handling

When a multi-statement query is executed through the readwritesplit router, it
will always be routed to the primary. See
[`strict_multi_stmt`](../Routers/ReadWriteSplit.md#strict_multi_stmt) for more
details.

If the multi-statement query creates a temporary table, it will not be
detected and reads to this table can be routed to replica servers. To
prevent this, always execute the temporary table creation as an individual
statement.

#### Limitations in client session handling

Some of the queries that a client sends are routed to all backends instead of
just to one. These queries include `USE <db name>` and `SET autocommit=0`, among
many others. Readwritesplit sends a copy of these queries to each backend server
and forwards the primary's reply to the client. Below is a list of MySQL commands
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
the replicas and fails, it may be due to replication lag rather than the database
not existing. Thus, the same command may produce different result in different
backend servers. The replicas which fail to execute a session command will be
dropped from the active list of replicas for this session to guarantee a
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
variables to the primary.
