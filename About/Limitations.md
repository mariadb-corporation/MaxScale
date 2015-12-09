# Limitations and Known Issues within MaxScale

The purpose of this documentation is to provide a central location that will document known issues and limitations within the MaxScale product and the plugins that form part of that product. Since limitations may related to specific plugins or to MaxScale as a whole this document is divided into a number of sections, the purpose of which are to isolate the limitations to the components which illustrate them.

## Limitations in the MaxScale core

This section describes the limitations that are common to all configuration of plugins with MaxScale.

## Limitations with MySQL Protocol support

Compression is not included in MySQL server handshake

## Limitations with Galera Cluster Monitoring

The default master selection is based only on MIN(wsrep_local_index). This can be influenced with the server priority mechanic described in the [Galera Monitor](../Monitors/Galera-Monitor.md) manual.

## Limitations in the connection router

* If Master changes (ie. new Master promotion) during current connection the router cannot check the change

* LONGBLOB is not supported

## Limitations in the Read/Write Splitter

### Scale-out limitations

In master-slave replication cluster also read-only queries are routed to master too in the following situations:

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
use_sql_variables_in=[master|all] (default: all)
```

Server-side session variables are called as SQL variables. If "master" is set, SQL variables are read and written in master only. Autocommit values and prepared statements are routed to all nodes always.

**NOTE**: If variable is written as a part of write query, it is treated like write query and not routed to all servers. For example, `INSERT INTO test.t1 VALUES (@myvar:= 7)` will not be routed and an error in the error log will be written. Add the `use_sql_variables_in=master` to the service definition to allow these queries.

#### Examples of session command limitations

If new database "db" was created and client executes “USE db” and it is routed to slave before the CREATE DATABASE clause is replicated to all slaves there is a risk of executing query in wrong database. Similarly, if any response that RWSplit sends back to the client differ from that of the master, there is a risk for misbehavior. To prevent this, any failures in session command execution are treated as fatal errors and all connections by the session to that particular slave server will be closed. In addition, the server will not used again for routing for the duration of the session.

Most imaginable reasons are related to replication lag but it could be possible that a slave fails to execute something because of some non-fatal, temporary failure while execution of same command succeeds in other backends.

## Authentication Related Limitations

MySQL old style passwords are not supported. MySQL versions 4.1 and newer use a new authentication protocol which does not support pre-4.1 style passwords.
