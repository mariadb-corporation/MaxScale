# SchemaRouter

The SchemaRouter provides an easy and manageable sharding solution by
building a single logical database server from multiple separate ones. Each
database is shown to the client and queries targeting unique databases are
routed to their respective servers. In addition to providing simple
database-based sharding, the schemarouter also enables cross-node
session variable usage by routing all queries that modify the session to all
nodes.

The main limitation of SchemaRouter is that aside from session variable writes
and some specific queries, a query can only target one server. This means that
queries which depend on results from multiple servers give incorrect results.
See [Limitations](#limitations) for more information.

From 2.3.0 onwards, SchemaRouter is capable of limited table family sharding.

Table of Contents
=================

* [Routing Logic](#routing-logic)
* [Configuration](#configuration)
* [Router Parameters](#router-parameters)
   * [ignore_databases](#ignore_databases)
   * [ignore_databases_regex](#ignore_databases_regex)
   * [preferred_server](#preferred_server)
* [Table Family Sharding](#table-family-sharding)
* [Router Options](#router-options)
   * [max_sescmd_history](#max_sescmd_history)
   * [disable_sescmd_history](#disable_sescmd_history)
   * [refresh_databases](#refresh_databases)
   * [refresh_interval](#refresh_interval)
* [Limitations](#limitations)
* [Examples](#examples)

## Routing Logic

If a command line client is used, i.e. `mysql`, and a direct connection to
the database is initialized without a default database, the router starts
with no default server where the queries are routed. This means that each
query that doesn't specify a database is routed to the first available
server.

If a `USE <database>` query is executed or a default database is defined
when connecting to MariaDB MaxScale, all queries without explicitly stated
databases will be routed to the server which has this database. If multiple
servers have the same database and the user connecting to MariaDB MaxScale
has rights to all of them, the database is associated to the first server
that responds when the databases are mapped. In practice this means that
query results will always come from a single server but the data might not
always be from the same node.

In almost all the cases these can be avoided by proper server configuration
and the databases are always mapped to the same servers. More on
configuration in the next chapter.

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

## Configuration

Here is an example configuration of the schemarouter:

```
[Shard-Router]
type=service
router=schemarouter
servers=server1,server2
user=myuser
passwd=mypwd
```

The module generates the list of databases based on the servers parameter
using the connecting client's credentials. The user and passwd parameters
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

### `ignore_databases`

List of databases to ignore when checking for duplicate databases.

### `ignore_databases_regex`

Regular expression that is matched against database names when checking for
duplicate databases.

### `preferred_server`

The name of a server in MaxScale which will be used as the preferred server
when a database is found on more than one server. If a database exists on
two servers, of which neither is the server referred by this parameter, the
server that replies first will be assigned as the location of the database.

This parameter allows deterministic conflict resolution when a sharded cluster
has a central database server and one or more sharded databases spread across
multiple servers which replicate from the central database server.

**Note:** As of version 2.1 of MaxScale, all of the router options can also be
defined as parameters. The values defined in _router_options_ will have priority
over the parameters.

```
[Shard-Router]
type=service
router=schemarouter
servers=server1,server2
user=myuser
passwd=mypwd
refresh_databases=true
refresh_interval=60
```

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

## Router Options

**Note:** Router options for the Schemarouter were deprecated in MaxScale 2.1.

The following options are options for the `router_options` parameter of the
service. Multiple router options are given as a comma separated list of key
value pairs.

### `max_sescmd_history`

Set a limit on the number of session modifying commands a session can execute.
This sets an effective cap on the memory consumption of the session.

### `disable_sescmd_history`

Disable the session command history. This will prevent growing memory consumption
of a long-running session and allows pooled connections to MariaDB MaxScale to be used.
The drawback of this is the fact that if a server goes down, the session state
will not be consistent anymore.

### `refresh_databases`

Enable database map refreshing mid-session. These are triggered by a failure to
change the database i.e. `USE ...` queries.

### `refresh_interval`

The minimum interval between database map refreshes in seconds.

## Limitations

1. Cross-database queries (e.g. `SELECT column FROM database1.table UNION select column
FROM database2.table`) are not properly supported. Such queries are routed either to the
first explicit database in the query, the current database in use or to the first
available database, depending on which succeeds.

* Without a default database, queries without explicit databases that do not modify the
session state will be routed to the first available server. This includes queries such as
`CREATE DATABASE db1`. Such queries should be done directly on the node or the router
should be equipped with the hint filter and a routing hint should be used. Queries that
modify the session state (e.g. `SET autocommit=1`) will be routed to all servers
regardless of the default database.

* SELECT queries that modify session variables are not supported because uniform results
can not be guaranteed. If such a query is executed, the behavior of the router is
undefined. To work around this limitation, the query must be executed in separate parts.

* If a query targets a database the SchemaRouter has not mapped to a server, the
query will be routed to the first available server. This possibly returns an
error about database rights instead of a missing database.

* The preparation of a prepared statement is routed to all servers. The
execution of a prepared statement is routed to the first available server or to
the server pointed by a routing hint attached to the query. In practice this
means that prepared statements aren't supported by the SchemaRouter.

* `SHOW DATABASES` is handled by the router instead of routed to a server. The router only
answers correctly to the basic version of the query. Any modifiers such as `LIKE` are
ignored.

* `SHOW TABLES` is routed to the server with the current database. If using table-level
sharding, the results will be incomplete. Use `SHOW SHARDS` to get results from the router
itself.

## Examples

[Here](../Tutorials/Simple-Sharding-Tutorial.md) is a small tutorial on how
to set up a sharded database.
