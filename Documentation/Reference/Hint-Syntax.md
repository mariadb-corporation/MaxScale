# Hint Syntax

## Enabling routing hints

To enable routing hints for a service, the hintfilter module needs to be configured and the filter needs to be applied to the service.

Here is an example service which has the hint filter configured and applied.

```
[Read Service]
type=service
router=readconnroute
router_options=master
servers=server1
user=maxuser
passwd=maxpwd
filters=Hint

[Hint]
type=filter
module=hintfilter

```

## Comments and comment types

The client connection will need to have comments enabled. For example the `mysql` command line client has comments disabled by default and they need to be enabled by passing the `-c` option.

For comment types, use either `-- ` (notice the whitespace after the double hyphen) or `#` after the semicolon or `/* .. */` before the semicolon.

The MySQL manual doesn't specify if comment blocks, i.e. `/* .. */`, should contain a
whitespace character before or after the tags, so adding whitespace at both the start and the end is advised.

## Hint body

All hints must start with the `maxscale` tag.

```
-- maxscale <hint body>
```

The hints have two types, ones that define a server type and others that contain
name-value pairs.

### Routing destination hints

These hints will instruct the router to route a query to a certain type of a server.
```
-- maxscale route to [master | slave | server <server name>]
```

A `master` value in a routing hint will route the query to a master server. This can be used to direct read queries to a master server for a up-to-date result with no replication lag. A `slave` value will route the query to a slave server. A `server` value will route the query to a named server. The value of <server name> needs to be the same as the server section name in maxscale.cnf.

### Name-value hints

These control the behavior and affect the routing decisions made by the router.

```
-- maxscale <param>=<value>
```

Currently the only accepted parameter is `max_slave_replication_lag`. This will route the query to a server with lower replication lag then what is defined in the hint value.

## Hint stack

Hints can be either single-use hints, which makes them affect only one query, or named
hints, which can be pushed on and off a stack of active hints.

Defining named hints:

```
-- maxscale <hint name> prepare <hint content>
```

Pushing a hint onto the stack:

```
-- maxscale <hint name> begin
```

Popping the topmost hint off the stack:

```
-- maxscale end
```

You can define and activate a hint in a single command using the following:

```
-- maxscale <hint name> begin <hint content>
```

You can also push anonymous hints onto the stack which are only used as long as they are on the stack:

```
-- maxscale begin <hint content>
```

## Examples

### Example 1 - Routing SELECT queries to master

In this example, MariaDB MaxScale is configured with the readwritesplit router and the hint filter.

```
[ReadWriteService]
type=service
router=readwritesplit
servers=server1,server2
user=maxuser
passwd=maxpwd
filters=Hint

[Hint]
type=filter
module=hintfilter
```

Behind MariaDB MaxScale is a master server and a slave server. If there is replication lag between the master and the slave, read queries sent to the slave might return old data. To guarantee up-to-date data, we can add a routing hint to the query.

```
INSERT INTO table1 VALUES ("John","Doe",1);
SELECT * from table1; -- maxscale route to master
```

The first INSERT query will be routed to the master. The following SELECT query would normally be routed to the slave but with the added routing hint it will be routed to the master. This way we can do an INSERT and a SELECT right after it and still get up-to-date data.
