# SchemaRouter

The SchemaRouter provides an easy and manageable sharding solution by
building a single logical database server from multiple separate ones. Each
database is shown to the client and queries targeting unique databases are
routed to their respective servers. In addition to providing simple
database-based sharding, the schemarouter also enables cross-node
session variable usage by routing all queries that modify the session to all
nodes.

By default the SchemaRouter assumes that each database and table is only located
on one server. If it finds the same database or table on multiple servers, it
will close the session with the following error:

```
ERROR 5000 (DUPDB): Error: duplicate tables found on two different shards.
```

If duplicate tables are expected, use the
[`ignore_tables_regex`](#ignore_tables_regex) parameter to controls which
duplicate tables are allowed. To disable the duplicate database detection, use
`ignore_tables_regex=.*`.

Schemarouter compares table and database names case-insensitively. This means
that the tables `test.t1` and `test.T1` are assumed to refer to the same table.

The main limitation of SchemaRouter is that aside from session variable writes
and some specific queries, a query can only target one server. This means that
queries which depend on results from multiple servers give incorrect results.
See [Limitations](#limitations) for more information.

From 2.3.0 onwards, SchemaRouter is capable of limited table family sharding.

## Changes in Version 6

* The `auth_all_servers` parameter is no longer automatically enabled by the
  schemarouter. To retain the old behavior that was present in 2.5, explicitly
  define `auth_all_servers=true` for all schemarouter services.

[TOC]

## Routing Logic

* If a command modifies the session state by modifying any session or user
  variables, the query is routed to all nodes. These statements include `SET`
  statements as well as any other statements that modify the behavior of the
  client.

* If a client changes the default database after connecting, either with a `USE
  <db>` query or a `COM_INIT_DB` command, the query is routed to all servers
  that contain the database. This same logic applies when a client connects with
  a default database: the default database is set only on servers that actually
  contain it.

* If a query targets one or more tables that the schemarouter has discovered
  during the database mapping phase, the query is only routed if a server is
  found that contains all of the tables that the query uses. If no such server
  is found, the query is routed to the server that was previously used or to the
  first available backend if none have been used. If a query uses a table but
  doesn't define the database it is in, it is assumed to be located on the
  default database of the connection.

* If a query uses a table that is unknown to the schemarouter or executes a
  command that doesn't target a table, the query is routed to a server contains
  the current active default database. If the connection does not have a default
  database, the query is routed to the backend that was last used or to the
  first available backend if none have been used. If the query contains a
  routing hint that directs it to a server, the query is routed there.

  This means that all administrative commands, replication related command as
  well as certain transaction control statements (XA transaction) are routed to
  the first available server in certain cases. To avoid problems, use routing
  hints to direct where these statements should go.

* Starting with MaxScale 6.4.5, transaction control commands (`BEGIN`, `COMMIT`
  and `ROLLBACK`) are routed to all nodes. Older versions of MaxScale routed the
  queries to the first available backend. This means that cross-shard
  transactions are technically possible but, without external synchronization,
  the transactions are not guaranteed to be globally consistent.

* `LOAD DATA LOCAL INFILE` commands are routed to the first available server
  that contains the tables listed in the query.

### Custom SQL Commands

To check how databases and tables map to servers, execute the special query
`SHOW SHARDS`. The query does not support any modifiers such as `LIKE`.

```
show shards;

Database |Server       |
---------|-------------|
db1.t1   |MyServer1    |
db1.t2   |MyServer1    |
db2.t1   |MyServer2    |
```

The schemarouter will also intercept the `SHOW DATABASES` command and generate
it based on its internal data. This means that newly created databases will not
show up immediately and will only be visible when the cached data has been
updated.

### Database Mapping

The schemarouter maps each of the servers to know where each database and table
is located. As each user has access to a different set of tables and databases,
the result is unique to the username and the set of servers that the service
uses. These results are cached by the schemarouter. The lifetime of the cached
result is controlled by the `refresh_interval` parameter.

When a server needs to be mapped, the schemarouter will route a query to each of
the servers using the client's credentials. While this query is being executed,
all other sessions that would otherwise share the cached result will wait for
the update to complete. This waiting functionality was added in MaxScale 2.4.19,
older versions did not wait for existing updates to finish and would perform
parallel database mapping queries.

## Configuration

Here is an example configuration of the schemarouter:

```
[Shard-Router]
type=service
router=schemarouter
servers=server1,server2
user=myuser
password=mypwd
```

The module generates the list of databases based on the servers parameter
using the connecting client's credentials. The user and password parameters
define the credentials that are used to fetch the authentication data from
the database servers. The credentials used only require the same grants as
mentioned in the configuration documentation.

The list of databases is built by sending a SHOW DATABASES query to all the
servers. This requires the user to have at least USAGE and SELECT grants on
the databases that need be sharded.

If you are connecting directly to a database or have different users on some
of the servers, you need to get the authentication data from all the
servers. You can control this with the `auth_all_servers` parameter. With
this parameter, MariaDB MaxScale forms a union of all the users and their
grants from all the servers. By default, the schemarouter will fetch the
authentication data from all servers.

For example, if two servers have the database `shard` and the following
rights are granted only on one server, all queries targeting the database
`shard` would be routed to the server where the grants were given.

```
# Execute this on both servers
CREATE USER 'john'@'%' IDENTIFIED BY 'password';

# Execute this only on the server where you want the queries to go
GRANT SELECT,USAGE ON shard.* TO 'john'@'%';
```

This would in effect allow the user 'john' to only see the database 'shard'
on this server. Take notice that these grants are matched against MariaDB
MaxScale's hostname instead of the client's hostname. Only user
authentication uses the client's hostname and all other grants use MariaDB
MaxScale's hostname.

## Router Parameters

### `ignore_tables`

List of full table names (e.g. db1.t1) to ignore when checking for duplicate
tables. By default no tables are ignored.

### `ignore_tables_regex`

A [PCRE2 regular expression](../Getting-Started/Configuration-Guide.md#regular-expressions)
that is matched against database names when checking for duplicate databases.
By default no tables are ignored.

The following configuration ignores duplicate tables in the databases `db1` and `db2`,
and all tables starting with "t" in `db3`.

```
[Shard-Router]
type=service
router=schemarouter
servers=server1,server2
user=myuser
password=mypwd
ignore_tables_regex=^db1|^db2|^db3\.t
```

### `preferred_server`

This parameter has been removed in MaxScale 6.0. It is no longer needed after
the fix to MXS-2793 made it possible to correctly store the database location
information.

### `ignore_databases`

This parameter has been removed in MaxScale 6.0, use
[ignore_tables](#ignore_tables) instead.

### `ignore_databases_regex`

This parameter has been removed in MaxScale 6.0, use
[ignore_tables_regex](#ignore_tables_regex) instead.

### `max_sescmd_history`

This parameter has been moved to
[the MaxScale core](../Getting-Started/Configuration-Guide.md#max_sescmd_history)
in MaxScale 6.0.

### `disable_sescmd_history`

This parameter has been moved to
[the MaxScale core](../Getting-Started/Configuration-Guide.md#disable_sescmd_history)
in MaxScale 6.0.

### `refresh_databases`

Enable database map refreshing mid-session. These are triggered by a failure to
change the database i.e. `USE ...` queries. This feature is disabled by default.

Before MaxScale 6.2.0, this parameter did nothing. Starting with the 6.2.0
release of MaxScale this parameter now works again but it is disabled by default
to retain the same behavior as in older releases.

### `refresh_interval`

The minimum interval between database map refreshes in seconds. The default
value is 300 seconds.

The interval is specified as documented
[here](../Getting-Started/Configuration-Guide.md#durations). If no explicit unit
is provided, the value is interpreted as seconds in MaxScale 2.4. In subsequent
versions a value without a unit may be rejected. Note that since the granularity
of the intervaltimeout is seconds, a timeout specified in milliseconds will be rejected,
even if the duration is longer than a second.

## Table Family Sharding

This functionality was introduced in 2.3.0.

If the same database exists on multiple servers, but the database contains different
tables in each server, SchemaRouter is capable of routing queries to the right server,
depending on which table is being addressed.

As an example, suppose the database `db` exists on servers _server1_ and _server2_, but
that the database on _server1_ contains the table `tbl1` and on _server2_ contains the
table `tbl2`. The query `SELECT * FROM db.tbl1` will be routed to _server1_ and the query
`SELECT * FROM db.tbl2` will be routed to _server2_. As in the example queries, the table
names must be qualified with the database names for table-level sharding to work.
Specifically, the query series below is not supported.

```
USE db;
SELECT * FROM tbl1; // May be routed to an incorrect backend if using table sharding.
```

## Router Diagnostics

The `router_diagnostics` output for a schemarouter service contains the
following fields.

* `queries`: Number of queries executed through this service.
* `sescmd_percentage`: The percentage of queries that were session commands.
* `longest_sescmd_chain`: The largest amount of session commands executed by one client session.
* `times_sescmd_limit_exceeded`: Number of times the session command history limit was exceeded.
* `longest_session`: The longest client session in seconds.
* `shortest_session`: The shortest client session in seconds.
* `average_session`: The average client session duration in seconds.
* `shard_map_hits`: Cache hits for the shard map cache.
* `shard_map_misses`: Cache misses for the shard map cache.

## Limitations

* Cross-database queries (e.g. `SELECT column FROM database1.table UNION select column
FROM database2.table`) are not properly supported. Such queries are routed either to the
first explicit database in the query, the current database in use or to the first
available database, depending on which succeeds.

* Without a default database, queries that do not use fully qualified table
names and which do not modify the session state (e.g. `SELECT * FROM t1`) will
be routed to the first available server. This includes queries such as explicit
transaction commands (`BEGIN`, `COMMIT`, `ROLLBACK`), all non-table `CREATE`
commands (`CREATE DATABASE`, `CREATE SEQUENCE`) as well as any `SELECT`
statements that do not directly refer to a table. `CREATE` commands should be
done directly on the node or the router should be equipped with the hint filter
and a routing hint should be used. Queries that modify the session state
(e.g. `SET autocommit=1`) will be routed to all servers regardless of the
default database. For explicit transactions, the recommended way is to use `SET
autocommit=0` to start a transaction and `SET autocommit=1` to commit it,
otherwise routing hints are required to correctly route the transaction control
commands. [MXS-4467](https://jira.mariadb.org/browse/MXS-4467) changed the
routing of transaction control commands to route them to all servers used by the
schemarouter.

* SELECT queries that modify session variables are not supported because uniform results
can not be guaranteed. If such a query is executed, the behavior of the router is
undefined. To work around this limitation, the query must be executed in separate parts.

* If a query targets a database the SchemaRouter has not mapped to a server, the
query will be routed to the first available server. This possibly returns an
error about database rights instead of a missing database.

* Prepared statement support is limited. PREPARE, EXECUTE and DEALLOCATE are routed to the
correct backend if the statement is known and only requires one backend server. EXECUTE
IMMEDIATE is not supported and is routed to the first available backend and may give
wrong results. Similarly, preparing a statement from a variable (e.g. `PREPARE stmt FROM
@a`) is not supported and may be routed wrong.

* `SHOW DATABASES` is handled by the router instead of routed to a server. The router only
answers correctly to the basic version of the query. Any modifiers such as `LIKE` are
ignored. Starting with MaxScale 22.08, the database names will always be in lowercase.

* `SHOW TABLES` is routed to the server with the current database. If using
table-level sharding, the results will be incomplete. Similarly, `SHOW TABLES
FROM db1` is routed to the server with database `db1`, ignoring table
sharding. Use `SHOW SHARDS` to get results from the router itself. Starting with
MaxScale 22.08, the database names will always be in lowercase.

* `USE db1` is routed to the server with `db1`. If the database is divided to multiple
servers, only one server will get the command.

## Examples

[Here](../Tutorials/Simple-Sharding-Tutorial.md) is a small tutorial on how
to set up a sharded database.
