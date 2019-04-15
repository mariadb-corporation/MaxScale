# Clustrix Monitor

## Overview

The Clustrix Monitor is a monitor that monitors a Clustrix cluster. It is
capable of detecting the cluster setup and creating corresponding server
instances within MaxScale.

## Configuration

A minimal configuration for a monitor requires one server in the Clustrix
cluster, and a username and a password to connect to the server. Note that
by default the Clustrix monitor will only use that server in order to
dynamically find out the configuration of the cluster; after startup it
will completely rely upon information obtained at runtime. To change the
default behaviour, please see the parameter
[dynamic_node_detection](#dynamic_node_detection).

To ensure that the Clustrix monitor will be able to start, it is adviseable
to provide _more_ than one server to cater for the case that not all nodes
are always up when MaxScale starts.

```
[TheClustrixMonitor]
type=monitor
module=clustrixmon
servers=server1,server2,server3
user=myuser
password=mypwd

```

## Dynamic Servers

The server objects the Clustrix monitor creates for each detected
Clustrix node will be named like
```
@@<name-of-clustrix-monitor>:node-<id>
```
where `<name-of-clustrix-monitor>` is the name of the Clustrix monitor
instance, as defined in the MaxScale configuration file, and `<id>` is the
id of the Clustrix node.

For instance, with the Clustrix monitor defined as above and a Clustrix
cluster consisting of 3 nodes whose ids are `1`, `2` and `3` respectively,
the names of the created server objects will be:
```
@@TheClustrixMonitor:node-1
@@TheClustrixMonitor:node-2
@@TheClustrixMonitor:node-3
```

### Grants

Note that the monitor user _must_ have `SELECT` grant on the following tables:

   * `system.nodeinfo`
   * `system.membership`
   * `system.softfailed_nodes`

You can give the necessary grants using the following commands:
```
    GRANT SELECT ON system.membership TO 'myuser'@'%';
    GRANT SELECT ON system.nodeinfo TO 'myuser'@'%';
    GRANT SELECT ON system.softfailed_nodes TO 'myuser'@'%';
```
Further, if you want be able to _softfail_ and _unsoftfail_ a node via MaxScale,
then the monitor user must have `SUPER` privileges, which can be granted like:
```
    GRANT SUPER ON *.* TO 'myuser'@'%';
```
The user name must be changed to the one actually being used.

## Common Monitor Parameters

For a list of optional parameters that all monitors support, read the
[Monitor Common](Monitor-Common.md) document.

## Clustrix Monitor optional parameters

These are optional parameters specific to the Clustrix Monitor.

### `cluster_monitor_interval`

Defines, in milliseconds, how often the monitor checks the state of the
entire cluster. The default value is 60000 (1 minute), which should not
be lowered as that may have an adverse effect on the Cluster itself.

```
cluster_monitor_interval=120000
```

### `health_check_threshold`

Defines how many times the health check may fail before the monitor
considers a particular node to be down. The default value is 2.

```
health_check_threshold=3
```

### `dynamic_node_detection`

By default, the Clustrix monitor will only use the bootstrap nodes
in order to connect to the Clustrux cluster and then find out the
cluster configuration dynamically at runtime.

That behaviour can be turned off with this optional parameter, in
which case all Clustrix nodes must manually be defined as shown below.

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

[Clustrix-Monitor]
type=monitor
module=clustrixmon
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

The Clustrix monitor supports the following module commands.

### `softfail`

With the `softfail` module command, a node can be _softfailed_ via
MaxScale. The command requires as argument the name of the Clustrix
monitor instance (as defined in the configuration file) and the name
of the node to be softfailed.

For instance, with a configuration file like
```
[TheClustrixMonitor]
type=monitor
module=clustrixmon
...
```
then the node whose server name is `@@TheClustrixMonitor:node-1` can
be softfailed like
```
$ maxctrl call command clustrixmon softfail TheClustrixMonitor @@TheClustrixMonitor:node-1
```
If a node is successfully softfailed, then the status of the corresponding
MaxScale server object will be set to `Draining`, which will prevent
new connections from being created to the node.

### `unsoftfail`

With the `unsoftfail` module command, a node can be _unsoftfailed_ via
MaxScale. The command requires as argument the name of the Clustrix
monitor instance (as defined in the configuration file) and the name
of the node to be unsoftfailed.

With a setup similar to the `softfail` case, a node can be unsoftfailed
like:
```
$ maxctrl call command clustrixmon unsoftfail TheClustrixMonitor @@TheClustrixMonitor:node-1
```
If a node is successfully softfailed, then a `Draining` status of
the corresponding MaxScale server object will be cleared.

## SOFTFAILed nodes

During the cluster check, which is performed once per
`cluster_monitor_interval`, the monitor will also check whether any
nodes are being softfailed. The status of the corresponding server
object of a node being softfailed will be set to `Draining`,
which will prevent new connections from being created to that node.

If a node that was softfailed is UNSOFTFAILed then the `Draining`
status will be cleared.

If the softfailing and unsoftfailing is initiated using the `softfail`
and `unsoftfail` commands of the Clustrix monitor, then there will be
no delay between the softfailing or unsoftfailing being initated and the
`Draining` status being turned on/off.
