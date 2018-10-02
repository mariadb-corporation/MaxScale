# Readwritesplit

This document provides a short overview of the **readwritesplit** router module
and its intended use case scenarios. It also displays all router configuration
parameters with their descriptions. A list of current limitations of the module
is included and use examples are provided.

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

**`max_slave_connections`** sets the maximum number of slaves a router session
uses at any moment. The default is to use all available slaves.

	max_slave_connections=<max. number, or % of available slaves>

### `max_slave_replication_lag`

**`max_slave_replication_lag`** specifies how many seconds a slave is allowed to
be behind the master. If the lag is bigger than the configured value a slave
can't be used for routing.

This feature is disabled by default.

	max_slave_replication_lag=<allowed lag in seconds>

This applies to Master/Slave replication with MySQL monitor and
`detect_replication_lag=1` options set. max_slave_replication_lag must be
greater than the monitor interval.

This option only affects Master-Slave clusters. Galera clusters do not have a
concept of slave lag even if the application of write sets might have lag.

### `use_sql_variables_in`

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
[Splitter Service]
type=service
router=readwritesplit
servers=dbserv1, dbserv2, dbserv3
user=maxscale
password=96F99AA1315BDC3604B006F427DD9484
disable_sescmd_history=true
master_failure_mode=fail_on_write
```

### `connection_keepalive`

Send keepalive pings to backend servers. This feature was introduced in MaxScale
2.2.0 and is disabled by default.

The parameter value is the interval in seconds between each keepalive ping. A
keepalive ping will be sent to a backend server if the connection is idle and it
has not been used within `n` seconds where `n` is greater than or equal to the
value of _connection_keepalive_. The keepalive pings are only sent when the
client executes a query.

This functionality allows the readwritesplit module to keep all backend
connections alive even if they are not used. This is a common problem if the
backend servers have a low _wait_timeout_ value and the client connections live
for a long time.

### `master_reconnection`

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

The `LEAST_GLOBAL_CONNECTIONS` and `LEAST_ROUTER_CONNECTIONS` use the
connections from MariaDB MaxScale to the server, not the amount of connections
reported by the server itself.

`LEAST_BEHIND_MASTER` does not take server weights into account when choosing a
server.

#### Server Weights and `slave_selection_criteria`

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

**`max_sescmd_history`** sets a limit on how many distinct session commands each
session can execute before the session command history is disabled. The default
is 50 session commands.

```
# Set a limit on the session command history
max_sescmd_history=1500
```

The first and last execution of each session command is stored. This means that
with `N` distinct session commands, the minimum value of `max_sescmd_history` to
guarantee that all of them are kept in the history is `N * 2`. In practice, the
real history size required to store the commands is closer to `N`.

If you have long-running sessions which change the session state often, increase
the value of this parameter if server reconnections fail due to disabled session
command history.

When a limitation is set, it effectively creates a cap on the session's memory
consumption. This might be useful if connection pooling is used and the sessions
use large amounts of session commands.

### `disable_sescmd_history`

This option disables the session command history. This way no history is stored
and if a slave server fails, the router will not try to replace the failed
slave. Disabling session command history will allow long-lived connections
without causing a constant growth in the memory consumption.

This option is only intended to be enabled if the value of
`max_slave_connections` is lowered below the default value. This will allow a
failed slave to be replaced with a standby slave server.

In versions 2.0 and older, the session command history is enabled by default.
In version 2.1 and 2.2, the session command history is disabled by default.  In
2.3 and newer versions, the session command is enabled but it is limited to a
default of 50 session commands after which the history is disabled.

```
# Disable the session command history
disable_sescmd_history=true
```

### `master_accept_reads`

**`master_accept_reads`** allows the master server to be used for reads. This is
a useful option to enable if you are using a small number of servers and wish to
use the master for reads as well.

By default, no reads are sent to the master.

```
# Use the master for reads
master_accept_reads=true
```

### `strict_multi_stmt`

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

Similar to `strict_multi_stmt`, this option allows all queries after a CALL
operation on a stored procedure to be routed to the master. This option is
disabled by default and was added in MaxScale 2.1.9.

All warnings and restrictions that apply to `strict_multi_stmt` also apply to
`strict_sp_calls`.

### `master_failure_mode`

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
that in _fail_on_write_ or _error_on_write_ mode, connections are accepted as
long as slave servers are available.

**Note:** If _master_failure_mode_ is set to _error_on_write_ and the connection
to the master is lost, by default, clients will not be able to execute write
queries without reconnecting to MariaDB MaxScale once a new master is
available. If [`master_reconnection`](#master_reconnection) is enabled, the
session can recover if one of the slaves is promoted as the master.

### `retry_failed_reads`

This option controls whether autocommit selects are retried in case of failure.
This option is enabled by default.

When a simple autocommit select is being executed outside of a transaction and
the slave server where the query is being executed fails, readwritesplit can
retry the read on a replacement server. This makes the failure of a slave
transparent to the client.

### `delayed_retry`

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

The number of seconds to wait until an error is returned to the client when
`delayed_retry` is enabled. The default value is 10 seconds.

### `transaction_replay`

Replay interrupted transactions. This parameter was added in MaxScale 2.3.0 and
is disabled by default. Enabling this parameter implicitly enables both the
`delayed_retry` and `master_reconnection` parameters.

When the server where the transaction is in progress fails, readwritesplit can
migrate the transaction to a replacement server. This can completely hide the
failure of a master node without any visible effects to the client.

If no replacement node becomes available before the timeout controlled by
`delayed_retry_timeout` is exceeded, the client connection is closed.

Not all transactions can be safely replayed. Only when the following criteria
are met, the transaction can be safely replayed.

* Transaction contains only data modification (`INSERT`, `UPDATE`, `DELETE`
  etc.) or `SELECT ... FOR UPDATE` statements.

* The replacement server where the transaction is applied returns results
  identical to the original partial transaction.

If the results from the replacement server are not identical when the transaction is
replayed, the client connection is closed. This means that any transaction with a server
specific result (e.g. `NOW()`, `@@server_id`) cannot be replayed successfully.

Performing MVCC reads (`SELECT` queries without `FOR UPDATE` or `LOCK IN SHARE MODE`)
with transaction replay is discouraged. If such statements are executed
but the results of each reply are identical, the transaction is replayed but the results
are not guaranteed to be consistent on the database level.

### `transaction_replay_max_size`

The limit on transaction size for transaction replay in bytes. Any transaction
that exceeds this limit will not be replayed. The default size limit is 1
MiB. Read [the configuration guide](../Getting-Started/Configuration-Guide.md#sizes)
for more details on size type parameters in MaxScale.

### `optimistic_trx`

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

Enable causal reads. This parameter is disabled by default and was introduced in
MaxScale 2.3.0.

If a client connection modifies the database and `causal_reads` is enabled, any
subsequent reads performed on slave servers will be done in a manner that
prevents replication lag from affecting the results. This only applies to the
modifications done by the client itself.

**Note:** This feature requires MariaDB 10.2.16 or newer to function. In
  addition to this, the `session_track_system_variables` parameter must be set
  to `last_gtid`.

A practical example can be given by the following set of SQL commands executed
with `autocommit=1`.

```sql
INSERT INTO test.t1 (id) VALUES (1);
SELECT * FROM test.t1 WHERE id = 1;
```

As the statements are not executed inside a transaction, from the load balancers
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
           WHEN MASTER_GTID_WAIT('0-3000-8', 120) = 0 THEN 1
           ELSE (SELECT 1 FROM INFORMATION_SCHEMA.ENGINES)
    END);
SELECT * FROM test.t1 WHERE id = 1;
```

The `SET` command will synchronize the slave to a certain logical point in
the replication stream (see
[MASTER_GTID_WAIT](https://mariadb.com/kb/en/library/master_gtid_wait/)
for more details). If the slave has not caught up to the master within the
configured time, an error will be returned. To the client side
application, this will appear as an error on the statement that they were
performing. This is caused by the fact that the synchronization command is
executed with the original command as a multi-statement command.

### `causal_reads_timeout`

The timeout for the slave synchronization done by `causal_reads`. The
default value is 120 seconds.

## Routing hints

The readwritesplit router supports routing hints. For a detailed guide on hint
syntax and functionality, please read [this](../Reference/Hint-Syntax.md)
document.

**Note**: Routing hints will always have the highest priority when a routing
decision is made. This means that it is possible to cause inconsistencies in
the session state and the actual data in the database by adding routing hints
to DDL/DML statements which are then directed to slave servers. Only use routing
hints when you are sure that they can cause no harm.

## Limitations

For a list of readwritesplit limitations, please read the
[Limitations](../About/Limitations.md) document.

## Legacy Configuration

In older versions of MaxScale, routers were configured via the _router_options_
parameter. This functionality was deprecated in 2.2 and was removed in 2.3.

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

* write statements,
* all statements within an open transaction,
* stored procedure calls
* user-defined function calls
* DDL statements (`DROP`|`CREATE`|`ALTER TABLE` … etc.)
* `EXECUTE` (prepared) statements that modify the database
* all statements using temporary tables

In addition to these, if the **readwritesplit** service is configured with the
`max_slave_replication_lag` parameter, and if all slaves suffer from too much
replication lag, then statements will be routed to the _Master_. (There might be
other similar configuration parameters in the future which limit the number of
statements that will be routed to slaves.)

### Routing to Slaves

The ability to route some statements to slaves is important because it also
decreases the load targeted to master. Moreover, it is possible to have multiple
slaves to share the load in contrast to single master.

Queries which can be routed to slaves must be auto committed and belong to one
of the following group:

* read-only database queries,
* read-only queries to system, or user-defined variables,
* `SHOW` statements
* system function calls.

### Routing to every session backend

A third class of statements includes those which modify session data, such as
session system variables, user-defined variables, the default database, etc. We
call them session commands, and they must be replicated as they affect the
future results of read and write operations. They must be executed on all
servers that could execute statements on behalf of this client.

Session commands include for example:

* `SET` statements
* `USE `*`<dbname>`*
* system/user-defined variable assignments embedded in read-only statements, such
as `SELECT (@myvar := 5)`
* `PREPARE` statements
* `QUIT`, `PING`, `STMT RESET`, `CHANGE USER`, etc. commands

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
