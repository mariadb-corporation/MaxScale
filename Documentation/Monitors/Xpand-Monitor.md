# Xpand Monitor

**NOTE** The Xpand monitor is intended for use with a native Xpand
cluster, not with the Xpand storage engine.

## Overview

The Xpand Monitor is a monitor that monitors a Xpand cluster. It is
capable of detecting the cluster setup and creating corresponding server
instances within MaxScale.

## Required Grants

The monitor user _must_ have the following grants:

```
CREATE USER 'maxscale'@'maxscalehost' IDENTIFIED BY 'maxscale-password';
GRANT SELECT ON system.membership TO 'maxscale'@'maxscalehost';
GRANT SELECT ON system.nodeinfo TO 'maxscale'@'maxscalehost';
GRANT SELECT ON system.softfailed_nodes TO 'maxscale'@'maxscalehost';
```

Further, if you want be able to _softfail_ and _unsoftfail_ a node via MaxScale,
then the monitor user must have `SUPER` privileges:

```
GRANT SUPER ON *.* TO 'maxscale'@'maxscalehost';
```

## Configuration

A minimal configuration for a monitor requires one server in the Xpand
cluster, and a username and a password to connect to the server. Note that
by default the Xpand monitor will only use that server in order to
dynamically find out the configuration of the cluster; after startup it
will completely rely upon information obtained at runtime. To change the
default behaviour, please see the parameter
[dynamic_node_detection](#dynamic_node_detection).

To ensure that the Xpand monitor will be able to start, it is adviseable
to provide _more_ than one server to cater for the case that not all nodes
are always up when MaxScale starts.

```
[TheXpandMonitor]
type=monitor
module=xpandmon
servers=server1,server2,server3
user=myuser
password=mypwd

```

## Dynamic Servers

The server objects the Xpand monitor creates for each detected
Xpand node will be named like
```
@@<name-of-xpand-monitor>:node-<id>
```
where `<name-of-xpand-monitor>` is the name of the Xpand monitor
instance, as defined in the MaxScale configuration file, and `<id>` is the
id of the Xpand node.

For instance, with the Xpand monitor defined as above and a Xpand
cluster consisting of 3 nodes whose ids are `1`, `2` and `3` respectively,
the names of the created server objects will be:
```
@@TheXpandMonitor:node-1
@@TheXpandMonitor:node-2
@@TheXpandMonitor:node-3
```

## Common Monitor Parameters

For a list of optional parameters that all monitors support, read the
[Monitor Common](Monitor-Common.md) document.

## Xpand Monitor optional parameters

These are optional parameters specific to the Xpand Monitor.

### `cluster_monitor_interval`

Defines, in milliseconds, how often the monitor checks the state of the
entire cluster. The default value is 60000 (1 minute), which should not
be lowered as that may have an adverse effect on the Cluster itself.

```
cluster_monitor_interval=120000ms
```
The interval is specified as documented
[here](../Getting-Started/Configuration-Guide.md#durations). If no explicit unit
is provided, the value is interpreted as milliseconds in MaxScale 2.4. In subsequent
versions a value without a unit may be rejected.

### `health_check_threshold`

Defines how many times the health check may fail before the monitor
considers a particular node to be down. The default value is 2.

```
health_check_threshold=3
```

### `dynamic_node_detection`

By default, the Xpand monitor will only use the bootstrap nodes
in order to connect to the Xpand cluster and then find out the
cluster configuration dynamically at runtime.

That behaviour can be turned off with this optional parameter, in
which case all Xpand nodes must manually be defined as shown below.

```
[Node-1]
type=server
address=192.168.121.77
port=3306
...

[Node-2]
...

[Node-3]
...

[TheXpandMonitor]
type=monitor
module=xpandmon
servers=Node-1, Node-2, Node-3
dynamic_node_detection=false
```

The default value of `dynamic_node_detection` is `true`.

See also [health_check_port](#health_check_port).

### `health_check_port`

With this optional parameter it can be specified what health check
port to use, if `dynamic_node_detection` has been disabled.

```
health_check_port=4711
```
The default value is `3581`.

Note that this parameter is _ignored_ unless `dynamic_node_detection`
is `false`. Note also that the port must be the same for all nodes.

## Commands

The Xpand monitor supports the following module commands.

### `softfail`

With the `softfail` module command, a node can be _softfailed_ via
MaxScale. The command requires as argument the name of the Xpand
monitor instance (as defined in the configuration file) and the name
of the node to be softfailed.

For instance, with a configuration file like
```
[TheXpandMonitor]
type=monitor
module=xpandmon
...
```
then the node whose server name is `@@TheXpandMonitor:node-1` can
be softfailed like
```
$ maxctrl call command xpandmon softfail TheXpandMonitor @@TheXpandMonitor:node-1
```
If the softfailing of a node is successfully initiated, then the status
of the corresponding MaxScale server object will be set to `Draining`,
which will prevent new connections from being created to the node.

When the number of connections through MaxScale to the node has dropped
to 0, its state will change to `Drained`. Note that the state `Drained`
only tells that there are no connections to the node, not what the state
of the softfailing operation is.

### `unsoftfail`

With the `unsoftfail` module command, a node can be _unsoftfailed_ via
MaxScale. The command requires as argument the name of the Xpand
monitor instance (as defined in the configuration file) and the name
of the node to be unsoftfailed.

With a setup similar to the `softfail` case, a node can be unsoftfailed
like:
```
$ maxctrl call command xpandmon unsoftfail TheXpandMonitor @@TheXpandMonitor:node-1
```
If a node is successfully softfailed, then a `Draining` status of
the corresponding MaxScale server object will be cleared.

## SOFTFAILed nodes

During the cluster check, which is performed once per
`cluster_monitor_interval`, the monitor will also check whether any
nodes are being softfailed. The status of the corresponding server
object of a node being softfailed will be set to `Draining`,
which will prevent new connections from being created to that node.

When the number of connections through MaxScale to the node has dropped
to 0, its state will change to `Drained`. Note that the state `Drained`
only tells that there are no connections to the node, not what the state
of the softfailing operation is.

If a node that was softfailed is UNSOFTFAILed then the `Draining`
status will be cleared.

If the softfailing and unsoftfailing is initiated using the `softfail`
and `unsoftfail` commands of the Xpand monitor, then there will be
no delay between the softfailing or unsoftfailing being initiated and the
`Draining` status being turned on/off.
