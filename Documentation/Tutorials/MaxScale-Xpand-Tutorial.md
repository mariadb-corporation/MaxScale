# MaxScale and Xpand Tutorial

**NOTE** The Xpand monitor is intended for use with a native Xpand
cluster, not with the Xpand storage engine.

Since version 2.4, MaxScale has built-in support for Xpand. This
tutorial explains how to setup MaxScale in front of a Xpand
cluster.

There is no Xpand specific router, but both the
[readconnroute](../Routers/ReadConnRoute.md) and
the [readwritesplit](../Routers/ReadWriteSplit.md) routers can be
used.

## Xpand and Readconnroute

With _readconnroute_ you get simple connection based routing, where
each new connection is created (by default) to the Xpand node with
the least amount of existing connections. That is, with readconnroute
the behaviour will be very similar to the behaviour when
[HAProxy](http://www.haproxy.org) is used as the Xpand load
balancer.

### Bootstrap servers

The Xpand monitor is capable of autonomously figuring out the cluster
configuration, but in order to get going there must be at least one
_server_-section referring to a node in the Xpand cluster.
```
[Bootstrap-1]
type=server
address=IP-OF-NODE
port=3306
protocol=MySQLBackend
```
That server defintion will be used by the monitor in order to connect
to the Xpand cluster. There can be more than one such "bootstrap"
definition to cater for the case that the node used as a bootstrap
server is down when MaxScale starts.

**NOTE** These bootstrap servers should _only_ be referred to from the
 Xpand monitor configuration, but _never_ from a service.

### Monitor

In the Xpand monitor section, the bootstrap servers are referred to
in the same way as "ordinary" servers are referred to in other monitors.
```
[Xpand]
type=monitor
module=xpandmon
servers=Bootstrap-1
user=USER
password=PASSWORD
```
The bootstrap servers are only used for connecting to the Xpand
cluster; thereafter the Xpand monitor will dynamically find out the
cluster configuration.

The discovered cluster configuration will be stored (the ips and ports
of the Xpand nodes) and upon subsequent restarts the Xpand
monitor will use that information if the bootstrap servers happen to
be unavailable.

With the configuration above `maxctrl list servers` might output
the following:
```
┌───────────────────┬──────────────┬──────┬─────────────┬─────────────────┬──────┐
│ Server            │ Address      │ Port │ Connections │ State           │ GTID │
├───────────────────┼──────────────┼──────┼─────────────┼─────────────────┼──────┤
│ @@Xpand:node-7    │ 10.2.224.102 │ 3306 │ 0           │ Master, Running │      │
├───────────────────┼──────────────┼──────┼─────────────┼─────────────────┼──────┤
│ @@Xpand:node-8    │ 10.2.224.103 │ 3306 │ 0           │ Master, Running │      │
├───────────────────┼──────────────┼──────┼─────────────┼─────────────────┼──────┤
│ @@Xpand:node-6    │ 10.2.224.101 │ 3306 │ 0           │ Master, Running │      │
├───────────────────┼──────────────┼──────┼─────────────┼─────────────────┼──────┤
│ Bootstrap-1       │ 10.2.224.101 │ 3306 │ 0           │ Master, Running │      │
└───────────────────┴──────────────┴──────┴─────────────┴─────────────────┴──────┘
```
All servers whose name start with `@@` have been detected dynamically.

Note that the address `10.2.224.101` appears twice; once for
`Bootstrap-1` and another time for `@@Xpand:node-6`. The Xpand
monitor will create a dynamic server instance for _all_ nodes in the
Xpand cluster; also for the ones used in bootstrap server sections.

### Service

The service is specified as follows:
```
[Xpand-Service]
type=service
router=readconnroute
user=USER
password=PASSWORD
cluster=Xpand
```
Note that the service does *not* list any specific servers, but
instead refers, using the argument `cluster`, to the Xpand monitor.

In practice this means that the service will use the servers of the
monitor named `Xpand` and in the case of a Xpand monitor those
servers will be the ones that the monitor has detected
dynamically. That is, when setup like this, the service will
automatically adjust to any changes taking place in the Xpand
cluster.

**NOTE** There is no need to specify any `router_options`, but the
default `router_options=running` provides the desired behaviour.
In particular do **not** specify `router_options=master` as that will
cause only a _single_ node to be used.

### Listener

To complete the configuration, a listener must be specified.
```
[Xpand-Service-Listener]
type=listener
service=Xpand-Service
protocol=MariaDBClient
port=4008
```

## Xpand and Readwritesplit

The primary purpose of the router _readwritesplit_ is to split
statements between one master and multiple slaves. In the case of
Xpand, all servers will be masters, but _readwritesplit_ may still
be the right choise.

Namely, as _readwritesplit_ is transaction aware and capable of
replaying transactions, it can be used for hiding certain events
taking place in Xpand from the clients that use it.

For instance, whenever a node is removed from or added to a Xpand
cluster there will be a _group change_, which is visible to a client
as a transaction rollback. However, if _readwritesplit_ is used and
transaction replay is enabled, then MaxScale may be able to hide the
group change so that the client only detects a slight delay.

Apart from the service section, the configuration when using
_readwritesplit_ is identical to the _readconnroute_ configuration
described above.

### Service

The service is specified as follows:
```
[Xpand-Service]
type=service
router=readwritesplit
user=maxscale
password=maxscale
cluster=Xpand
transaction_replay=true
slave_selection_criteria=LEAST_GLOBAL_CONNECTIONS
```
With this configuration, subject to the boundary conditions of
transaction replaying, a client will neither notice group change
events nor the disappearance of the very node the client is connected
to. In that latter case, MaxScale will simply connect to another node
and replay the current transaction (if one is active). For detailed
information about the transaction replay functionality, please refer
to the _readwritesplit_
[documentation](../Routers/ReadWriteSplit.md#transaction_replay).

**NOTE** It is vital to have
`slave_selection_criteria=LEAST_GLOBAL_CONNECTIONS`, as otherwise
connections will **not** be distributed evenly across all Xpand
nodes.

As a rule of thumb, use _readwritesplit_ if it is important that
changes taking place in the cluster configuration are hidden from the
applications, otherwise use _readconnroute_.
