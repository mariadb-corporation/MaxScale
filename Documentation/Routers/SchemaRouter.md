# SchemaRouter Router

The SchemaRouter router provides an easy and manageable sharding SOLUTION by
building a single logical database server from multiple separate ones. Each
database is shown to the client and queries targeting unique databases are
routed to their respective servers. In addition to providing simple
database-based sharding, the schemarouter router also enables cross-node
session variable usage by routing all queries that modify the session to all
nodes.

From 2.3.0 onwards, the SchemaRouter is capable of sharding tables, in
addition to sharding databases.

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

## Configuration

Here is an example configuration of the schemarouter router:

```
[Shard Router]
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
[Shard Router]
type=service
router=schemarouter
servers=server1,server2
user=myuser
passwd=mypwd
refresh_databases=true
refresh_interval=60
```

## Table Sharding

This functionality was introduced in 2.3.0.

If the same database exists on multiple servers, but the database contains
different tables in each server, the SchemaRouter is capable of
transparently routing queries to the right server, depending on which table
is being addressed.

For instance, suppose the database `db` exists on servers _server1_ and
_server2_, but that the database on _server1_ contains the table `tbl1` and
on _server2_ contains the table `tbl2`.

In that case, the query
```
SELECT * FROM db.tbl1
```
will be routed to _server1_ and the query
```
SELECT * FROM db.tbl2
```
will be routed to _server2_.

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

For a list of schemarouter limitations, please read the
[Limitations](../About/Limitations.md) document.

## Examples

[Here](../Tutorials/Simple-Sharding-Tutorial.md) is a small tutorial on how
to set up a sharded database.
