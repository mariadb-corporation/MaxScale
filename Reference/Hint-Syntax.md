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
filter=Hint

[Hint]
type=filter
module=hintfilter

```

## Comments and comment types

The client connection will need to have comments enabled. For example the `mysql` command line client has comments disabled by default.

For comment types, use either `-- ` (notice the whitespace) or `#` after the semicolon or `/* .. */` before the semicolon. All comment types work with routing hints.

The MySQL manual doesn`t specify if comment blocks, i.e. `/* .. */`, should contain a w
hitespace character before or after the tags, so adding whitespace at both the start and the end is advised.

## Hint body

All hints must start with the `maxscale` tag.

```
-- maxscale <hint body>
```	

The hints have two types, ones that route to a server and others that contain
name-value pairs.

###Routing destination hints

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
