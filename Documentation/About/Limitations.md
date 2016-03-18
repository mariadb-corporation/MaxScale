# Limitations and Known Issues within MaxScale

The purpose of this documentation is to provide a central location that
will document known issues and limitations within the MaxScale product and
the plugins that form part of that product. Since limitations may related
to specific plugins or to MaxScale as a whole this document is divided
into a number of sections, the purpose of which are to isolate the
limitations to the components which illustrate them.

## Limitations in the MaxScale core

This section describes the limitations that are common to all
configuration of plugins with MaxScale.

## Limitations with MySQL Protocol support

Compression is not included in MySQL server handshake

## Limitations with Galera Cluster Monitoring

The default master selection is based only on MIN(wsrep_local_index). This
can be influenced with the server priority mechanic described in the
[Galera Monitor](../Monitors/Galera-Monitor.md) manual.

## Limitations in the connection router

* If Master changes (ie. new Master promotion) during current connection
  the router cannot check the change.

* Sending of LONGBLOB data is not supported

## Limitations in the Read/Write Splitter

Read queries are routed to the master server in the following situations:

* if they are executed inside an open transaction

* in case of prepared statement execution

* statement includes a stored procedure, or an UDF call

* if there are multiple statements inside one query e.g.
  `INSERT INTO ... ; SELECT LAST_INSERT_ID();`

### Limitations in multi-statement handling

When a multi-statement query is executed through the readwritesplit
router, it will always be routed to the master. With the default
configuration, all queries after a multi-statement query will be routed to
the master to prevent possible reads of false data.

You can override this behavior with the `strict_multi_stmt=false` router
option. In this mode, the multi-statement queries will still be routed to
the master but individual statements are routed normally. If you use
multi-statements and you know they don't modify the session state in any
relevant way, you can disable this option for better performance.

For more information, read the
[ReadWriteSplit](../Routers/ReadWriteSplit.md) router documentation.

### Parsing related limitations

Galera Cluster variables, such as @@wsrep_node_name, are not resolved by
the embedded MariaDB parser. This usually means that the query will be
routed to the master.

### Limitations in client session handling

Some of the queries that client sends are routed to all backends instead
of sending them just to one of server. These queries include `USE <db
name>` and `SET autocommit=0` among many others. Readwritesplit sends a
copy of these queries to each backend server and forwards the master's
reply to the client. Below is a list of MySQL commands which are
classified as session commands :

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

There is a possibility for misbehavior; if `USE mytable` was executed in
one of the slaves and it failed, it may be due to replication lag rather
than the fact it didn’t exist. Thus the same command may end up with
different result among backend servers. The slaves which fail to execute a
session command will be dropped from the active list of slaves for this
session to guarantee a consistent session state across all the servers
that are in use by the session.

The above-mentioned behavior can be partially controller with the
`use_sql_variables_in` configuration parameter.

```
use_sql_variables_in=[master|all] (default: all)
```

Server-side session variables are called as SQL variables. If "master" is
set, SQL variables are read and written in master only. Autocommit values
and prepared statements are routed to all nodes always.

**WARNING**

If a SELECT query modifies a user variable when the `use_sql_variables_in`
parameter is set to `all`, it will not be routed and the client will receive
an error. A log message is written into the log further explaining the reason
for the error. Here is an example use of a SELECT query which modifies a user
variable and how MaxScale responds to it.

```
MySQL [(none)]> set @id=1;
Query OK, 0 rows affected (0.00 sec)

MySQL [(none)]> SELECT @id := @id + 1 FROM test.t1;
ERROR 1064 (42000): Routing query to backend failed. See the error log for further details.
```

You allow user variable modification in SELECT queries by setting the
value of `use_sql_variables_in` to `master`. This will route all queries
that use user variables to the master.

#### Examples of session command limitations

If a new database "db" was created and client executes “USE db” and it is
routed to a slave before the CREATE DATABASE clause is replicated to all
slaves, there is a risk of executing a query in the wrong database. Similarly, if
any response that RWSplit sends back to the client differ from that of the
master, there is a risk for misbehavior. To prevent this, any failures in
session command execution are treated as fatal errors and all connections
by the session to that particular slave server will be closed. In
addition, the server will not used again for routing for the duration of
the session.

The most likely reasons are related to replication lag but it could be
possible that a slave fails to execute something because of some
non-fatal, temporary failure, while the execution of the same command
succeeds in other backends.

## Authentication Related Limitations

* MaxScale can not manage authentication that uses wildcard matching in hostnames
  in the mysql.user table of the backend database. The only wildcards that can be
  used are in IP address entries.

* MySQL old style passwords are not supported. MySQL versions 4.1 and newer use
  a new authentication protocol which does not support pre-4.1 style passwords.

* When users have different passwords based on the host from which they connect
  MaxScale is unable to determine which password it should use to connect to the
  backend database. This results in failed connections and unusable usernames
  in MaxScale.

## Schemarouter limitations

The schemarouter router currently has some limitations due to the nature of
the sharding implementation and the way the session variables are detected
and routed. Here is a list of the current limitations.

* Cross-database queries (e.g.
  `SELECT column FROM database1.table UNION select column FROM database2.table`)
  are not supported and are routed either to the first explicit database
  in the query, the current database in use or to the first available
  database, if none of the previous conditions are met.

* Without a default database, queries without explicit databases that do not
  modify the session state will be routed the first available server. This
  means that, for example when creating a new database, queries should
  be done directly on the node or the router should be equipped with
  the hint filter and a routing hint should be used. Queries that
  modify the session state e.g. `SET autocommit=1` will be routed
  to all servers regardless of the default database.

* SELECT queries that modify session variables are not currently supported
  because uniform results can not be guaranteed. If such a query is
  executed, the behavior of the router is undefined. To work around this
  limitation the query must be executed in separate parts.

* If a query targets a database the schemarouter hasn't mapped to a server
  the query will be routed to the first available server. This possibly
  returns an error about database rights instead of a missing database.

## Dbfwfilter limitations

The Database Firewall filter does not support multi-statements. Using them
will result in an error being sent to the client.
