# Readconnroute

This document provides an overview of the **readconnroute** router module and its intended use case scenarios. It also displays all router configuration parameters with their descriptions.

## Overview

The readconnroute router provides simple and lightweight load balancing across a set of servers. The router can also be configured to balance connections based on a weighting parameter defined in the server's section.

## Configuration

For more details about the standard service parameters, refer to the [Configuration Guide](../Getting-Started/Configuration-Guide.md).

### Router Options

**`router_options`** can contain a list of valid server roles. These roles are used as the valid types of servers the router will form connections to when new sessions are created.
```
	router_options=slave
```
Here is a list of all possible values for the `router_options`.

Role|Description
------|---------
master|A server assigned as a master by one of MariaDB MaxScale monitors. Depending on the monitor implementation, this could be a master server of a Master-Slave replication cluster or a Write-Master of a Galera cluster.
slave|A server assigned as a slave of a master. If all slaves are down, but the master is still available, then the router will use the master.
synced| A Galera cluster node which is in a synced state with the cluster.
ndb|A MySQL Replication Cluster node
running|A server that is up and running. All servers that MariaDB MaxScale can connect to are labeled as running.

If no `router_options` parameter is configured in the service definition, the router will use the default value of `running`. This means that it will load balance connections across all running servers defined in the `servers` parameter of the service.

When a connection is being created and the candidate server is being chosen, the
list of servers is processed in from first entry to last. This means that if two
servers with equal weight and status are found, the one that's listed first in
the _servers_ parameter for the service is chosen.

## Limitations

For a list of readconnroute limitations, please read the [Limitations](../About/Limitations.md) document.

## Examples

The most common use for the readconnroute is to provide either a read or write port for an application. This provides a more lightweight routing solution than the more complex readwritesplit router but requires the application to be able to use distinct write and read ports.

To configure a read-only service that tolerates master failures, we first need to add a new section in to the configuration file.

```
[Read Service]
type=service
router=readconnroute
servers=slave1,slave2,slave3
router_options=slave
```

Here the `router_options` designates slaves as the only valid server type. With this configuration, the queries are load balanced across the slave servers.

For more complex examples of the readconnroute router, take a look at the examples in the [Tutorials](../Tutorials) folder.
