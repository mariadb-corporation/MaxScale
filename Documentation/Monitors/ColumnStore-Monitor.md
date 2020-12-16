# ColumnStore Monitor

The ColumnStore monitor, `csmon`, is a monitor module for MariaDB ColumnStore
servers. The monitor supports ColumnStore version 1.5.

## Required Grants

The credentials defined with the `user` and `password` parameters must have all
grants on the `infinidb_vtable` database.

For example, to create a user for this monitor with the required grants execute
the following SQL.

```
CREATE USER 'maxscale'@'maxscalehost' IDENTIFIED BY 'maxscale-password';
GRANT ALL ON infinidb_vtable.* TO 'maxscale'@'maxscalehost';
```

## Configuration

Read the [Monitor Common](Monitor-Common.md) document for a list of supported
common monitor parameters.

### `version`

With this _deprecated_ optional parameter the used ColumnStore version is
specified. The only allowed value is `1.5`.

### `admin_port`

This optional parameter specifies the port of the ColumnStore administrative
daemon. The default value is `8640`. Note that the daemons of all nodes must
be listening on the same port.

### `admin_base_path`

This optional parameter specifies the base path of the ColumnStore
administrative daemon. The default value is `/cmapi/0.4.0`.

### `api_key`

This optional parameter specifies the API key to be used in the
communication with the ColumnStore administrative daemon. If no
key is specified, then a key will be generated and stored to the
file `api_key.txt` in the directory with the same name as the
monitor in data directory of MaxScale. Typically that will
be `/var/lib/maxscale/<monitor-section>/api_key.txt`.

Note that ColumnStore will store the first key provided and
thereafter require it, so changing the key requires the
resetting of the key on the ColumnStore nodes as well.

### `local_address`

With this parameter it is specified what IP MaxScale should
tell the ColumnStore nodes it resides at. Either it or
`local_address` at the global level in the MaxScale
configuration file must be specified. If both have been
specified, then the one specified for the monitor overrides.

### `dynamic_node_detection`

This optional boolean parameter specifies whether the monitor should
autonomously figure out the ColumnStore cluster configuration or whether
it should solely rely upon the monitor configuration in the configuration
file. Please see [Dynamic Node Detection](#dynamic-node-detection) for a
thorough discussion on the meaning of the parameter. The default value
is `false`.

### `cluster_monitor_interval`

This optional parameter, meaningful only if `dynamic_node_detection` is
`true` specifies how often the monitor should probe the ColumnStore
cluster and adapt to any changes that have occurred in the number of
nodes of the cluster. The default value is `10s`, that is, the
cluster configuration is probed every 10 seconds.

Note that as the probing is performed at the regular monitor round,
the value should be some multiple of `monitor_interval`.

## Dynamic Node Detection

**NOTE** If dynamic node detection is used, the network setup must
be such that the hostname/IP-address of a ColumnStore node is the
same when viewed both from MaxScale and from another node.

By default, the ColumnStore monitor behaves like the regular MariaDB
monitor. That is, it only monitors the servers it has been configured
with.

If `dynamic_node_detection` has been enabled, the behaviour of the monitor
changes significantly. Instead of being explicitly told which servers it
should monitor, the monitor is only told how to get into contact with the
cluster whereafter it autonomously figures out the cluster configuration
and creates dynamic server entries accordingly.

When dynamic node detection is enabled, the servers the monitor has been
configured with are _only_ used for "bootstrapping" the monitor, because
at the initial startup the monitor does not otherwise know how to get
into contact with the cluster.

In the following is shown a configuration using dynamic node detection.
```
[CsBootstrap1]
type=server
address=mcs1
port=3306
protocol=mariadbbackend

[CsBootstrap2]
type=server
address=mcs2
port=3306
protocol=mariadbbackend

[CsMonitor]
type=monitor
module=csmon
servers=CsBootstrap1, CsBootstrap2
dynamic_node_detection=true
...
```
As can be seen, the server entries look just like any other server entries,
but to make them stand out and to indicate what they are used for, they have
the word _bootstrap_ in their name.

In principle, it is sufficient with a single entry, but to cater for the
case that a node happens to be down, it is adviseable to have more than one.
Once the monitor has been able to connect to a node, it will fetch the
configuration and store information about the nodes locally. On subsequent
startups, the monitor will use the bootstrap information only if it cannot
connect using the persisted information. Also, if there has been any change
in the bootstrap servers, the persisted information is not used.

Based on the information obtained from the cluster itself, the monitor
will create _dynamic_ server instances that are named as `@@` followed by
the monitor name, followed by a `:`, followed by the hostname.

If the cluster in fact consists of three nodes, then the output of
`maxctrl list servers` may look like

```
┌──────────────────┬─────────┬──────┬─────────────┬─────────────────┬──────┐
│ Server           │ Address │ Port │ Connections │ State           │ GTID │
├──────────────────┼─────────┼──────┼─────────────┼─────────────────┼──────┤
│ @@CSMonitor:mcs2 │ mcs2    │ 3306 │ 0           │ Slave, Running  │      │
├──────────────────┼─────────┼──────┼─────────────┼─────────────────┼──────┤
│ @@CSMonitor:mcs3 │ mcs3    │ 3306 │ 0           │ Master, Running │      │
├──────────────────┼─────────┼──────┼─────────────┼─────────────────┼──────┤
│ @@CSMonitor:mcs1 │ mcs1    │ 3306 │ 0           │ Slave, Running  │      │
├──────────────────┼─────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsBootstrap1     │ mcs1    │ 3306 │ 0           │ Slave, Running  │      │
├──────────────────┼─────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsBootstrap2     │ mcs2    │ 3306 │ 0           │ Slave, Running  │      │
└──────────────────┴─────────┴──────┴─────────────┴─────────────────┴──────┘
```

Note that there will be dynamic server entries _also_ for the nodes for
which there is a bootstrap entry.

When the service is defined, it is imperative that it does not explicitly
refer to either the bootstrap or the dynamic entries. Instead, it should
refer to the monitor using the `cluster` parameter.
```
[RWS]
type=service
router=readwritesplit
cluster=CsMonitor
...
```
With this configuration the _RWS_ service will automatically adapt to any
changes made to the ColumnStore cluster.

## Commands

The ColumnStore monitor provides module commands using which the ColumnStore
cluster can be managed. The commands can be invoked using the REST-API with
a client such as curl or using maxctrl.

All commands require the monitor instance name as the first parameters.
Additional parameters must be provided depending on the command.

Note that as maxctrl itself has a timeout of 10 seconds, if a
timeout larger than that is provided to any command, the timeout of
maxctrl must also be increased. For instance:
```
maxctrl --timeout 30s call command csmon shutdown CsMonitor 20s
```
Here a 30 second timeout is specified for maxctrl to ensure
that it does not expire before the timeout of 20s provided for
the shutdown command possibly does.

The output is always a JSON object.

In the following, assume a configuration like this:
```
[CsNode1]
type=server
...

[CsNode2]
type=server
...

[CsMonitor]
type=monitor
module=csmon
servers=CsNode1,CsNode2
...

```

### `start`
Starts the ColumnStore cluster.
```
call command csmon start <monitor-name> <timeout>
```

Example
```
call command csmon start CsMonitor 20s
```

### `shutdown`
Shuts down the ColumnStore cluster.
```
call command csmon shutdown <monitor-name> <timeout>
```

Example
```
call command csmon shutdown CsMonitor 20s
```

### `status`
Get the status of the ColumnStore cluster.
```
call command csmon status <monitor-name> [<server>]
```
Returns the status of the cluster or the status of a specific server.

Example
```
call command csmon status CsMonitor
call command csmon status CsMonitor CsNode1
```

### `mode-set`
Sets the mode of the cluster.
```
call command csmon mode-set <monitor-name> (readonly|readwrite) <timeout>
```

Example
```
call command csmon mode-set CsMonitor readonly 20s
```

### `config-get`
Returns the cluster configuration.
```
call command csmon config-get <monitor-name> [<server-name>]
```
If no server is specified, the configuration is fetched from
the first server in the monitor configuration, otherwise from
the specified server.

Note that if everything is in order, the returned configuration
should be identical regardless of the server it is fetched from.

Example
```
call command csmon config-get CsMonitor CsNode2
```

### `add-node`
Adds a new node located on the server at the hostname or IP _host_
to the ColumnStore cluster.
```
call command csmon add-node <monitor-name> <host> <timeout>
```

Example
```
call command csmon add-node CsMonitor mcs2 20s
```
For a more complete example, please refer to [adding a node](#adding-a-node).

### `remove-node`
Remove the node located on the server at the hostname or IP _host_
from the ColumnStore cluster.
```
call command csmon remove-node <monitor-name> <host> <timeout>
```

Example
```
call command csmon remove-node CsMonitor mcs2 20s
```
For a more complete example, please refer to [removing a node](#removing-a-node).

## Example

The following is an example of a `csmon` configuration.

```
[CSMonitor]
type=monitor
module=csmon
version=1.5
servers=CsNode1,CsNode2
user=myuser
password=mypwd
monitor_interval=5000
api_key=somekey1234
```

## Adding a Node

Note that in the following `dynamic_node_detection` is not used, but
the monitor is configured in the traditional way. The impact of
`dynamic_node_detection` is described [here](#impact-of-dynamic_node_detection).

Adding a new node to a ColumnStore cluster can be performed dynamically
at runtime, but it must be done in two steps. First, the node is added
to ColumnStore and then, the corresponding server object (that possibly
has to be created) in the MaxScale configuration is added to the
ColumnStore monitor.

In the following, assume a two node ColumnStore cluster and an initial
MaxScale configuration like.
```
[CsNode1]
type=server
...

[CsNode2]
type=server
...

[CsMonitor]
type=monitor
module=csmon
servers=CsNode1,CsNode2
...
```
Invoking `maxctrl list servers` will now show:
```
┌─────────┬─────────────┬──────┬─────────────┬─────────────────┬──────┐
│ Server  │ Address     │ Port │ Connections │ State           │ GTID │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode1 │ 10.10.10.10 │ 3306 │ 0           │ Master, Running │      │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode2 │ 10.10.10.11 │ 3306 │ 0           │ Slave, Running  │      │
└─────────┴─────────────┴──────┴─────────────┴─────────────────┴──────┘
```
If we now want to add a new ColumnStore node, located at `mcs3/10.10.10.12`
to the cluster, the steps are as follows.

First the node is added
```
maxctrl --timeout 30s call command csmon add-node CsMonitor mcs3 20s
```
After a while the following is output:
```
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/modules/csmon/add-node"
    },
    "meta": {
        "message": "Node mcs3 successfully added to cluster.",
        "result": {
            "node_id": "mcs3",
            "timestamp": "2020-08-07 10:03:49.474539"
        },
        "success": true
    }
}
```
At this point, the ColumnStore cluster consists of three nodes. However,
the ColumnStore monitor is not yet aware of the new node.

First we need to create the corresponding server object.
```
maxctrl create server CsNode3 10.10.10.12
```
Invoking `maxctrl list servers` will now show:
```
┌─────────┬─────────────┬──────┬─────────────┬─────────────────┬──────┐
│ Server  │ Address     │ Port │ Connections │ State           │ GTID │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode3 │ 10.10.10.12 │ 3306 │ 0           │ Down            │      │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode1 │ 10.10.10.10 │ 3306 │ 0           │ Master, Running │      │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode2 │ 10.10.10.11 │ 3306 │ 0           │ Slave, Running  │      │
└─────────┴─────────────┴──────┴─────────────┴─────────────────┴──────┘
```
The server `CsNode3` has been created, but its state is `Down` since
it is not yet being monitored.
```
┌───────────┬─────────┬──────────────────┐
│ Monitor   │ State   │ Servers          │
├───────────┼─────────┼──────────────────┤
│ CsMonitor │ Running │ CsNode1, CsNode2 │
└───────────┴─────────┴──────────────────┘
```
It must now be added to the monitor.
```
maxctrl link monitor CsMonitor CsNode3
```
Now the server is monitored and `maxctrl list monitors` shows:
```
┌───────────┬─────────┬───────────────────────────┐
│ Monitor   │ State   │ Servers                   │
├───────────┼─────────┼───────────────────────────┤
│ CsMonitor │ Running │ CsNode1, CsNode2, CsNode3 │
└───────────┴─────────┴───────────────────────────┘
```
The state of the new node is now also set correctly, as shown by
`maxctrl list servers`.
```
┌─────────┬─────────────┬──────┬─────────────┬─────────────────┬──────┐
│ Server  │ Address     │ Port │ Connections │ State           │ GTID │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode3 │ 10.10.10.12 │ 3306 │ 0           │ Slave, Running  │      │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode1 │ 10.10.10.10 │ 3306 │ 0           │ Master, Running │      │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode2 │ 10.10.10.11 │ 3306 │ 0           │ Slave, Running  │      │
└─────────┴─────────────┴──────┴─────────────┴─────────────────┴──────┘
```
Note that the MaxScale server object can be created at any point, but
it must not be added to the monitor before the node has been added to
the ColumnStore cluster using `call command csmon add-node`.

### Impact of `dynamic_node_detection`

If `dynamic_node_detection` is enabled, there is no need to create any
explicit server entries. All that needs to be done, is to add the node
and the monitor will adapt automatically. Note that it does not matter
whether the node is added indirectly via maxscale or directly using the
REST-API of ColumnStore. The only difference is that in the former case,
MaxScale may detect the new situation slightly faster.

## Removing a Node

Note that in the following `dynamic_node_detection` is not used, but
the monitor is configured in the traditional way. The impact of
`dynamic_node_detection` is described [here](#impact-of-dynamic-node-detection-1).

Removing a node should be performed in the reverse order of how a
node was added. First, the MaxScale server should be removed from the
monitor. Then, the node should be removed from the ColumnStore cluster.

Suppose we want to remove the ColumnStore node at `mcs2/10.10.10.12`
and the current situation is as:
```
┌───────────┬─────────┬───────────────────────────┐
│ Monitor   │ State   │ Servers                   │
├───────────┼─────────┼───────────────────────────┤
│ CsMonitor │ Running │ CsNode1, CsNode2, CsNode3 │
└───────────┴─────────┴───────────────────────────┘
```
First, the server is removed from the monitor.
```
maxctrl unlink monitor CsMonitor CsNode3
```
Checking with `maxctrl list monitors` we see that the server has
indeed been removed.
```
┌───────────┬─────────┬──────────────────┐
│ Monitor   │ State   │ Servers          │
├───────────┼─────────┼──────────────────┤
│ CsMonitor │ Running │ CsNode1, CsNode2 │
└───────────┴─────────┴──────────────────┘
```
Now the node can be removed from the cluster itself.
```
maxctrl --timeout 30s call command csmon remove-node CsMonitor mcs3 20s
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/modules/csmon/remove-node"
    },
    "meta": {
        "message": "Node mcs3 removed from the cluster.",
        "result": {
            "node_id": "mcs3",
            "timestamp": "2020-08-07 11:41:36.573425"
        },
        "success": true
    }
}
```
### Impact of `dynamic_node_detection`

If `dynamic_node_detection` is enabled, there is in general no need
to explicitly remove a static server entry (as there never was one in
the first place). The only exception is if the removed node happened
to be a bootstrap server. In that case, the server entry should be
removed from the monitor's list of servers (used as bootstrap nodes).
If that is not done, then the monitor will log a warning at each startup.
