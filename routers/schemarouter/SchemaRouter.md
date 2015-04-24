#SchemaRouter Router

The SchemaRouter router provides an easy and manageable sharding solution by building a single logical database server from multiple separate ones. Each database is shown to the client and queries targeting unique databases are routed to their respective servers. In addition to providing simple database-based sharding, the schemarouter router also enables cross-node session variable usage by routing all queries that modify the session to all nodes.

## Routing Logic

If a command line client is used, i.e. `mysql`, and a direct connection to the database is initialized without a default database, the router starts with no default server where the queries are routed. This means that each query that doesn't specify a database is routed to the first available server.

If a `USE <database>` query is executed or a default database is defined when connecting to MaxScale, all queries without explicitly stated databases will be routed to the server which has this database. If multiple servers have the same database and the user connecting to MaxScale has rights to all of them, the database is associated to the first server that responds when the databases are mapped. In practice this means that query results will always come from a single server but the data might not always be from the same node.

In almost all the cases these can be avoided by proper server configuration and the databases are always mapped to the same servers. More on configuration in the next chapter.

## Configuration

Here is an example configuration of the schemarouter router:

```
Shard Router]
type=service
router=schemarouter
servers=server1,server2
user=myuser
passwd=mypwd
auth_all_servers=1
```

The module generates the list of databases based on the servers parameter using the connecting client's credentials. The user and passwd parameters define the credentials that are used to fetch the authentication data from the database servers. The credentials used only require the same grants as mentioned in the configuration documentation.

The list of databases is built by sending a SHOW DATABASES query to all the servers. This requires the user to have at least USAGE and SELECT grants on the databases that need be sharded. 

If you are connecting directly to a database or have different users on some of the servers, you need to get the authentication data from all the servers. You can control this with the `auth_all_servers` parameter. With this parameter, MaxScale forms a union of all the users and their grants from all the servers. By default, the schemarouter will fetch the authentication data from all servers.

For example, if two servers have the database 'shard' and the following rights are granted only on one server, all queries targeting the database 'shard' would be routed to the server where the grants were given.

```
# Execute this on both servers
CREATE USER 'john'@'%' IDENTIFIED BY 'password';

# Execute this only on the server where you want the queries to go
GRANT SELECT,USAGE ON shard.* TO 'john'@'%';
```

This would in effect allow the user 'john' to only see the database 'shard' on this server. Take notice that these grants are matched against MaxScale's hostname instead of the client's hostname. Only user authentication uses the client's hostname and all other grants use MaxScale's hostname.

The schemarouter supports the following router options:

|option				|parameter	|description|
---------------------------------------------
|max_sescmd_hitory	|<int>		|Set a limit on the number of session modifying commands a session can execute. This sets an effective cap on the memory consupmtion of the session.|
|disable_sescmd_history|<boolean>|Disable the session command history. This will prevent growing memory consumption of a long-running session and allows pooled connections to MaxScale to be used. The drawback of this is the fact that if a server goes down, the session state will not be consistent anymore.|
## Limitations

The schemarouter router currently has some limitations due to the nature of the sharding implementation and the way the session variables are detected and routed. Here is a list of the current limitations.

- Cross-database queries (e.g. `SELECT column FROM database1.table UNION select column FROM database2.table`) are not supported and are routed either to the first explicit database in the query, the current database in use or to the first available database, if none of the previous conditions are met.

- Queries without explicit databases that are not session commands in them are either routed to the current or the first available database. This means that, for example when creating a new database, queries should be done directly on the node or the router should be equipped with the hint filter and a routing hint should be used.

- Temporary tables are only created on the explicit database in the query or the current database in use. If no database is in use and no database is explicitly stated, the behavior of the router is undefined.

- SELECT queries that modify session variables are not currently supported because uniform results can not be guaranteed. If such a query is executed, the behavior of the router is undefined. To work around this limitation the query must be executed in separate parts.

- Queries targeting databases not mapped by the schemarouter router but still exist on the database server are not blocked but routed to the first available server. This possibly returns an error about database rights instead of a missing database. The behavior of the router is undefined in this case.

## Examples

[Here](../../Tutorials/Simple-Sharding-Tutorial.md) is a small tutorial on how to set up a sharded database.
