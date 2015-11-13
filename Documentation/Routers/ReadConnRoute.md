# Readconnroute

This document provides an overview of the **readconnroute** router module and its intended use case scenarios. It also displays all router configuration parameters with their descriptions.

## Overview

The readconnroute router provides simple and lightweight load balancing across a set of servers. The router can also be configured to balance connections based on a weighting parameter defined in the server's section.

## Configuration

Readconnroute router-specific settings are specified in the configuration file of MaxScale in its specific section. The section can be freely named but the name is used later as a reference from listener section.

The configuration consists of mandatory and optional parameters.

## Mandatory parameters

**`type`** specifies the type of service. For readconnroute module the type is `router`:

    type=router

**`router`** specifies the router module to be used. For readconnroute the value is `readconnroute`:

    router=readconnroute

**`servers`** provides a list of servers, which the router will connect to:

    servers=server1,server2,server3

**NOTE: Each server on the list must have its own section in the configuration file where it is defined.**

**`user`** is the username the router session uses for accessing backends in order to load the content of the `mysql.user` table (and `mysql.db` and database names as well) and optionally for creating, and using `maxscale_schema.replication_heartbeat` table.

**`passwd`** specifies corresponding password for the user. Syntax for user and passwd is:

```
user=<username>
passwd=<password>
```

## Optional parameters

The **`weightby`** parameter defines the name of the value which is used to calculate the weights of the servers. Here is an example server configuration with the `serv_weight` parameter used as the weighting parameter.

```
[server1]
type=server
address=127.0.0.1
port=3000
protocol=MySQLBackend
serv_weight=3

[server2]
type=server
address=127.0.0.1
port=3001
protocol=MySQLBackend
serv_weight=1

[Read Service]
type=service
router=readconnroute
servers=server1,server2
weightby=serv_weight
```

With this configuration and a heavy query load, the server *server1* will get most of the connections and about a third of the remaining queries are routed to the second server. With server weights, you can assign secondary servers that are only used when the primary server is under heavy load.

Without the weightby parameter, each connection counts as a single connection. With a weighting parameter, a single connection received its weight from the server's own weighting parameter divided by the sum of all weighting parameters in all the configured servers.

If we use the previous configuration as an example, the sum of the `serv_weight` parameter is 4. Server1 would receive a weight of `3/4=75%` and server2 would get `1/4=25%`. This means that server1 would get 75% of the connections and server2 would get 25% of the connections.

**`router_options`** can contain a list of valid server roles. These roles are used as the valid types of servers the router will form connections to when new sessions are created.
```
	router_options=slave
```
Here is a list of all possible values for the `router_options`.

Role|Description
------|---------
master|A server assigned as a master by one of MaxScale monitors. Depending on the monitor implementation, this could be a master server of a Master-Slave replication cluster or a Write-Master of a Galera cluster.
slave|A server assigned as a slave of a master.
synced| A Galera cluster node which is in a synced state with the cluster.
ndb|A MySQL Replication Cluster node
running|A server that is up and running. All servers that MaxScale can connect to are labeled as running.

If no `router_options` parameter is configured in the service definition, the router will use the default value of `running`. This means that it will load balance connections across all running servers defined in the `servers` parameter of the service.

## Examples

The most common use for the readconnroute is to provide either a read or write port for an application. This provides a more lightweight routing solution than the more complex readwritesplit router but requires the application to be able to use distinct write and read ports.

To configure a  read-only service that tolerates master failures, we first need to add a new section in to the configuration file.

```
[Read Service]
type=service
router=readconnroute
servers=slave1,slave2,slave3
router_options=slave
```

Here the `router_options`designates slaves as the only valid server type. With this configuration, the queries are load balanced across the slave servers.

For more complex examples of the readconnroute router, take a look at the examples in the [Tutorials](../Tutorials) folder.
