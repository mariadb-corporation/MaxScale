# Readwritesplit

This document provides a short overview of the **readwritesplit** router module and its intended use case scenarios. It also displays all router configuration parameters with their descriptions. A list of current limitations of the module is included and examples of the router's use are provided.

## Overview

The **readwritesplit** router is designed to increase the read-only processing capability of a cluster while maintaining consistency. This is achieved by splitting the query load into read and write queries. Read queries, which do not modify data, are spread across multiple nodes while all write queries will be sent to a single node. 

The router is designed to be used with a traditional Master-Slave replication cluster. It automatically detects changes in the master server and will use the current master server of the cluster. With a Galera cluster, one can achieve a resilient setup and easy master failover by using one of the Galera nodes as a Write-Master node, where all write queries are routed, and spreading the read load over all the nodes.

## Configuration

Readwritesplit router-specific settings are specified in the configuration file of MaxScale in its specific section. The section can be freely named but the name is used later as a reference from listener section.

The configuration consists of mandatory and optional parameters.

## Mandatory parameters

### `type`

**`type`** specifies the type of service. For **readwritesplit** module the type is `router`:

    type=router

### `router`

**`router`** specifies the router module to be used. For **readwritesplit** the value is `readwritesplit`:

    router=readwritesplit

### `servers`

**`servers`** provides a list of servers, which must include one master and available slaves:

    servers=server1,server2,server3

**NOTE: Each server on the list must have its own section in the configuration file where it is defined.**

### `user`
**`user`** is the username the router session uses for accessing backends in order to load the content of the `mysql.user` table (and `mysql.db` and database names as well) and optionally for creating, and using `maxscale_schema.replication_heartbeat` table.

### `passwd`

**`passwd`** specifies corresponding password for the user. Syntax for user and passwd is:

```
user=<username>
passwd=<password>
```

## Optional parameters

### `max_slave_connections`

**`max_slave_connections`** sets the maximum number of slaves a router session uses at any moment. The default is to use all available slaves.

	max_slave_connections=<max. number, or % of available slaves>

### `max_slave_replication_lag`
**`max_slave_replication_lag`** specifies how many seconds a slave is allowed to be behind the master. If the lag is bigger than configured value a slave can't be used for routing.

	max_slave_replication_lag=<allowed lag in seconds>

This applies to Master/Slave replication with MySQL monitor and `detect_replication_lag=1` options set.
Please note max_slave_replication_lag must be greater than monitor interval.


### `use_sql_variables_in`

**`use_sql_variables_in`** specifies where should queries, which read session variable, be routed. The syntax for `use_sql_variable_in` is:

    use_sql_variables_in=[master|all] (default: all)

When value all is used, queries reading session variables can be routed to any available slave (depending on selection criteria). Note, that queries modifying session variables are routed to all backend servers by default, excluding write queries with embedded session variable modifications, such as:

    INSERT INTO test.t1 VALUES (@myid:=@myid+1)

In above-mentioned case the user-defined variable would only be updated in the master where query would be routed due to `INSERT` statement.

## Router options

**`router_options`** may include multiple **readwritesplit**-specific options. All the options are parameter-value pairs. All parameters listed in this section must be configured as a value in `router_options`.

Multiple options can be defined as a comma-separated list of parameter-value pairs.

```
router_options=<option>,<option>
```

### `slave_selection_criteria`

This option controls how the readwritesplit router chooses the slaves it connects to and how the load balancing is done. The default behavior is to route read queries to the slave server with the lowest amount of ongoing queries.

The option syntax:

```
router_options=slave_selection_criteria=<criteria>
```

Where `<criteria>` is one of the following values.

* `LEAST_GLOBAL_CONNECTIONS`, the slave with least connections in total
* `LEAST_ROUTER_CONNECTIONS`, the slave with least connections from this router
* `LEAST_BEHIND_MASTER`, the slave with smallest replication lag
* `LEAST_CURRENT_OPERATIONS` (default), the slave with least active operations

### `max_sescmd_history`

**`max_sescmd_history`** sets a limit on how many session commands each session can execute before the session command history is disabled. The default is an unlimited number of session commands.

```
# Set a limit on the session command history
max_sescmd_history=1500
```

When a limitation is set, it effectively creates a cap on the session's memory consumption. This might be useful if connection pooling is used and the sessions use large amounts of session commands.

### `disable_sescmd_history`

**`disable_sescmd_history`** disables the session command history. This way no history is stored and if a slave server fails, the router will not try to replace the failed slave. Disabling session command history will allow connection pooling without causing a constant growth in the memory consumption.

```
# Disable the session command history
disable_sescmd_history=true
```

### `master_accept_reads`

**`master_accept_reads`** allows the master server to be used for reads. This is a useful option to enable if you are using a small number of servers and wish to use the master for reads as well.

```
# Use the master for reads
master_accept_reads=true
```

## Routing hints

The readwritesplit router supports routing hints. For a detailed guide on hint syntax and functionality, please read [this](../Reference/Hint-Syntax.md) document.

## Limitations

In Master-Slave replication cluster also read-only queries are routed to master too in the following situations:

* if they are executed inside an open transaction

* in case of prepared statement execution

* statement includes a stored procedure, or an UDF call

### Limitations in client session handling

Some of the queries that client sends are routed to all backends instead of sending them just to one of server. These queries include `USE <db name>` and `SET autocommit=0` among many others. Readwritesplit sends a copy of these queries to each backend server and forwards the master's reply to the client. Below is a list of MySQL commands which are classified as session commands :
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

There is a possibility for misbehavior; if `USE mytable` was executed in one of the slaves and it failed, it may be due to replication lag rather than the fact it didn’t exist. Thus the same command may end up with different result among backend servers. The slaves which fail to execute a session command will be dropped from the active list of slaves for this session to guarantee a consistent session state across all the servers that are in use by the session.

The above-mentioned behavior can be partially controller with the `use_sql_variables_in` configuration parameter.

```
use_sql_variables_in=[master|all] (master)
```

Server-side session variables are called as SQL variables. If "master" or no value is set, SQL variables are read and written in master only. Autocommit values and prepared statements are routed to all nodes always.

**NOTE**: If variable is written as a part of write query, it is treated like write query and not routed to all servers. For example, `INSERT INTO test.t1 VALUES (@myvar:= 7)` will be routed to the master and an error in the error log will be written.

### Examples of limitations

If new database "db" was created and client executes “USE db” and it is routed to slave before the CREATE DATABASE clause is replicated to all slaves there is a risk of executing query in wrong database. Similarly, if any response that RWSplit sends back to the client differ from that of the master, there is a risk for misbehavior. To prevent this, any failures in session command execution are treated as fatal errors and all connections by the session to that particular slave server will be closed. In addition, the server will not used again for routing for the duration of the session.

Most imaginable reasons are related to replication lag but it could be possible that a slave fails to execute something because of some non-fatal, temporary failure while execution of same command succeeds in other backends.

## Examples

Examples of the readwritesplit router in use can be found in the [Tutorials](../Tutorials) folder.

## Readwritesplit routing decisions

Here is a small explanation which shows what kinds of queries are routed to which type of server.

### Routing to Master

Routing to master is important for data consistency and because majority of writes are written to binlog and thus become replicated to slaves.

The following operations are routed to master:

* write statements,
* all statements within an open transaction,
* stored procedure calls, and
* user-defined function calls.
* DDL statements (`DROP`|`CREATE`|`ALTER TABLE` … etc.)
* `EXECUTE` (prepared) statements
* all statements using temporary tables

In addition to these, if the **readwritesplit** service is configured with the `max_slave_replication_lag` parameter, and if all slaves suffer from too much replication lag, then statements will be routed to the _Master_. (There might be other similar configuration parameters in the future which limit the number of statements that will be routed to slaves.)

### Routing to Slaves

The ability to route some statements to *Slave*s is important because it also decreases the load targeted to master. Moreover, it is possible to have multiple slaves to share the load in contrast to single master.

Queries which can be routed to slaves must be auto committed and belong to one of the following group:

* read-only database queries,
* read-only queries to system, or user-defined variables,
* `SHOW` statements, and
* system function calls.

### Routing to every session backend

A third class of statements includes those which modify session data, such as session system variables, user-defined variables, the default database, etc. We call them session commands, and they must be replicated as they affect the future results of read and write operations, so they must be executed on all servers that could execute statements on behalf of this client.

Session commands include for example:

* `SET` statements
* `USE `*`<dbname>`*
* system/user-defined variable assignments embedded in read-only statements, such as `SELECT (@myvar := 5)`
* `PREPARE` statements
* `QUIT`, `PING`, `STMT RESET`, `CHANGE USER`, etc. commands

**NOTE: if variable assignment is embedded in a write statement it is routed to _Master_ only. For example, `INSERT INTO t1 values(@myvar:=5, 7)` would be routed to _Master_ only.**

The router stores all of the executed session commands so that in case of a slave failure, a replacement slave can be chosen and the session command history can be repeated on that new slave. This means that the router stores each executed session command for the duration of the session. Applications that use long-running sessions might cause MaxScale to consume a growing amount of memory unless the sessions are closed. This can be solved by setting a connection timeout on the application side.
