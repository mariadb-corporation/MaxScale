# Readwritesplit

This document provides a short overview of the **readwritesplit** router module and its intended use case scenarios. It also displays all router configuration parameters with their descriptions. A list of current limitations of the module is included and examples of the router's use are provided.

## Overview

The **readwritesplit** router is designed to be used with a Master-Slave replication cluster. It automatically detects changes in the master server and will use the current master server of the cluster. With a Galera cluster, one can achieve a resilient setup and easy master failover by using one of the Galera nodes as a Write-Master node, where are write queries are routed, and spreading the read load over all the nodes.

## Configuration

Readwritesplit router-specific settings are specified in the configuration file of MaxScale in its specific section. The section can be freely named but the name is used later as a reference from listener section.

The configuration consists of mandatory and optional parameters.

## Mandatory parameters

**`type`** specifies the type of service. For **readwritesplit** module the type is `router`:

    type=router

**`router`** specifies the router module to be used. For **readwritesplit** the value is `readwritesplit`:

    router=readwritesplit

**`servers`** provides a list of servers, which must include one master and available slaves:

    servers=server1,server2,server3

**NOTE: Each server on the list must have its own section in the configuration file where it is defined.**

**`user`** is the username the router session uses for accessing backends in order to load the content of the `mysql.user` table (and `mysql.db` and database names as well) and optionally for creating, and using `maxscale_schema.replication_heartbeat` table.

**`passwd`** specifies corresponding password for the user. Syntax for user and passwd is:

```
user=<username>
passwd=<password>
```

## Optional parameters

**`max_slave_connections`** sets the maximum number of slaves a router session uses at any moment. Default value is `1`.

	max_slave_connections=<max. number, or % of available slaves>

**`max_slave_replication_lag`** specifies how many seconds a slave is allowed to be behind the master. If the lag is bigger than configured value a slave can't be used for routing.

	max_slave_replication_lag=<allowed lag in seconds>

This applies to Master/Slave replication with MySQL monitor and `detect_replication_lag=1` options set.
Please note max_slave_replication_lag must be greater than monitor interval.

**`router_options`** may include multiple **readwritesplit**-specific options. Values are either singular or parameter-value pairs. Currently available is a single option which specifies the criteria used in slave selection both in initialization of router session and per each query. Note that due to the current monitor implementation, the value specified here should be *<twice the monitor interval>* + 1.

	options=slave_selection_criteria=<criteria>

where *<criteria>* is one of the following:

* `LEAST_GLOBAL_CONNECTIONS`, the slave with least connections in total
* `LEAST_ROUTER_CONNECTIONS`, the slave with least connections from this router
* `LEAST_BEHIND_MASTER`, the slave with smallest replication lag
* `LEAST_CURRENT_OPERATIONS` (default), the slave with least active operations

**`use_sql_variables_in`** specifies where should queries, which read session variable, be routed. The syntax for `use_sql_variable_in` is:

    use_sql_variables_in=[master|all]

When value all is used, queries reading session variables can be routed to any available slave (depending on selection criteria). Note, that queries modifying session variables are routed to all backend servers by default, excluding write queries with embedded session variable modifications, such as:

    INSERT INTO test.t1 VALUES (@myid:=@myid+1)

In above-mentioned case the user-defined variable would only be updated in the master where query would be routed due to `INSERT` statement.

**`max_sescmd_history`** sets a limit on how many session commands each session can execute before the connection is closed. The default is an unlimited number of session commands.

	max_sescmd_history=1500

When a limitation is set, it effectively creates a cap on the session's memory consumption. This might be useful if connection pooling is used and the sessions use large amounts of session commands.

**`disable_sescmd_history`** disables the session command history. This way nothing is stored and if a slave server fails and a new one is taken in its stead, the session on that server will be in an inconsistent state compared to the master server. Disabling session command history will allow connection pooling without causing a constant growth in the memory consumption.

```
# Disable the session command history
disable_sescmd_history=true
```

**`disable_slave_recovery`** disables the recovery and replacement of slave servers. If this option is enabled and a connection to a slave server in use is lost, no replacement slave will be taken. This allows the safe use of session state modifying statements when the session command history is disabled. This is mostly intended to be used with the `disable_sescmd_history` option enabled.

```
# Disable the session command history
disable_slave_recovery=true
```

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
