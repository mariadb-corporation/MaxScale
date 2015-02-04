#DBShard Router

The DBShard router provides an easy and manageable sharding solution by building a single logical database server from multiple separate ones. Each database is shown to the client and queries targeting unique databases are routed to their respective servers. In addition to providing simple database-based sharding, the dbshard router also enables cross-node session variable usage by routing all queries that modify the session to all nodes.

## Configuration

Here is an example configuration of the dbshard router:

     [Shard Router]
     type=service
     router=dbshard
     servers=server1,server2
     user=myuser
     passwd=mypwd

The module generates the list of databases based on the servers parameter using the connecting client's credentials. The user and passwd parameters define the credentials that are used to fetch the authentication data from the database servers. The credentials used only require the same grants as mentioned in the configuration documentation.

The list of databases is built by sending a SHOW DATABASES query to all the servers. This requires the user to have at least USAGE and SELECT grants on the databases that need be sharded.

For example, if two servers have the database 'shard' and the following rights are granted only on one server, all queries targeting the database 'shard' would be routed to the server where the grants were given.

    # Execute this on both servers
    CREATE USER 'john'@'%' IDENTIFIED BY 'password';

    # Execute this only on the server where you want the queries to go
    GRANT SELECT,USAGE ON shard.* TO 'john'@'%';

This would in effect allow the user 'john' to only see the database 'shard' on this server. Take notice that these grants are matched against MaxScale's hostname instead of the client's hostname. Only user authentication uses the client's hostname and all other grants use MaxScale's hostname.

## Limitations

The dbshard router currently has some limitations due to the nature of the sharding implementation and the way the session variables are detected and routed. Cross-database queries (e.g. SELECT column FROM database1.table UNION select column FROM database2.table) are not supported and are routed either to the first explicit database in the query, the current database in use or to the first available database, if none of the previous conditions are met. Queries without explicit databases that are not session commands in them are either routed to the current or the first available database. This means that, for example when creating a new database, queries should be done directly on the node or the router should be equipped with the hint filter and a routing hint should be used.

Temporary tables are only created on the explicit database in the query or the current database in use. If no database is in use and no database is explicitly stated, the behavior of the router is undefined.

SELECT queries that modify session variables are not currently supported because uniform results can not be guaranteed. If such a query is executed, the behavior of the router is undefined. To work around this limitation the query must be executed in separate parts.

## Examples

To be implemeted.

## Technical Documentation

[Technical Overview and Lifecycle Walkthrough](DBShard-technical.md)
