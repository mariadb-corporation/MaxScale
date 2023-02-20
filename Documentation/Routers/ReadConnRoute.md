# Readconnroute

This document provides an overview of the **readconnroute** router module
and its intended use case scenarios. It also displays all router
configuration parameters with their descriptions.

[TOC]

## Overview

The readconnroute router provides simple and lightweight load balancing
across a set of servers. The router can also be configured to balance
connections based on a weighting parameter defined in the server's section.

Note that **readconnroute* balances _connections_ and not _statements_.
When a client connects, the router selects a server based upon the router
configuration and current server load, but the single created connection
is fixed and will not be changed for the duration of the session. If the
connection between MaxScale and the server breaks, the connection can not
be re-established and the session will be closed. The fact that the server
is fixed when the client connects also means that routing hints are ignored.

## Configuration

For more details about the standard service parameters, refer to the
[Configuration Guide](../Getting-Started/Configuration-Guide.md).

### `router_options`

- **Type**: [enum_mask](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `master`, `slave`, `synced`, `running`
- **Default**: `running`

**`router_options`** can contain a comma separated list of valid server
roles. These roles are used as the valid types of servers the router will
form connections to when new sessions are created.

Examples:
```
router_options=slave
router_options=master,slave
```
Here is a list of all possible values for the `router_options`.

Role|Description
------|---------
master|A server assigned as a primary by one of MariaDB MaxScale monitors. Depending on the monitor implementation, this could be a primary server of a Primary-Replica replication cluster or a Write-Primary of a Galera cluster.
slave|A server assigned as a replica of a primary. If all replicas are down, but the primary is still available, then the router will use the primary.
synced| A Galera cluster node which is in a synced state with the cluster.
running|A server that is up and running. All servers that MariaDB MaxScale can connect to are labeled as running.

If no `router_options` parameter is configured in the service definition,
the router will use the default value of `running`. This means that it will
load balance connections across all running servers defined in the `servers`
parameter of the service.

When a connection is being created and the candidate server is being chosen,
the list of servers is processed in from first entry to last. This means
that if two servers with equal weight and status are found, the one that's
listed first in the _servers_ parameter for the service is chosen.

### `master_accept_reads`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true

This option can be used to prevent queries from being sent to the current primary.
If `router_options` does not contain `master`, the readconnroute instance is
usually meant for reading. Setting `master_accept_reads=false` excludes the primary
from server selection (and thus from receiving reads).

If `router_options` contains `master`, the setting of `master_accept_reads` has no effect.

By default `master_accept_reads=true`.

### `max_replication_lag`

- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 0s

The maximum acceptable replication lag. The value is in seconds and is specified
as documented [here](../Getting-Started/Configuration-Guide.md#durations). This
feature is disabled by default.

The replication lag of a server must be less than the configured value in order
for it to be used for routing. To configure the router to not allow any lag, use
`max_slave_replication_lag=1`.

## Examples

The most common use for the readconnroute is to provide either a read or
write port for an application. This provides a more lightweight routing
solution than the more complex readwritesplit router but requires the
application to be able to use distinct write and read ports.

To configure a read-only service that tolerates primary failures, we first
need to add a new section into the configuration file.

```
[Read-Service]
type=service
router=readconnroute
servers=replica1,replica2,replica3
router_options=slave
```

Here the `router_options` designates replicas as the only valid server
type. With this configuration, the queries are load balanced across the
replica servers.

For more complex examples of the readconnroute router, take a look at the
examples in the [Tutorials](../Tutorials) folder.

## Router Diagnostics

The `router_diagnostics` output for readconnroute has the following fields.

* `queries`: Number of queries executed through this service.

## Limitations

* Sending of binary data with `LOAD DATA LOCAL INFILE` is not supported.

* The router will never reconnect to the server it initially connected to.
