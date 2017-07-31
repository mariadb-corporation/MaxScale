# Galera Monitor

## Overview

The Galera Monitor is a monitoring module for MaxScale that monitors a Galera cluster. It detects whether nodes are a part of the cluster and if they are in sync with the rest of the cluster. It can also assign master and slave roles inside MaxScale, allowing Galera clusters to be used with modules designed for traditional master-slave clusters.

By default, the Galera Monitor will choose the node with the lowest `wsrep_local_index`
value as the master. This will mean that two MaxScales running on different
servers will choose the same server as the master.

## Configuration

A minimal configuration for a  monitor requires a set of servers for monitoring and a username and a password to connect to these servers. The user requires the REPLICATION CLIENT privilege to successfully monitor the state of the servers.

```
[Galera Monitor]
type=monitor
module=galeramon
servers=server1,server2,server3
user=myuser
passwd=mypwd

```

## Common Monitor Parameters

For a list of optional parameters that all monitors support, read the [Monitor Common](Monitor-Common.md) document.

## Galera Monitor optional parameters

These are optional parameters specific to the Galera Monitor.

### `disable_master_failback`

If a node marked as master inside MaxScale happens to fail and the master status is assigned to another node MaxScale will normally return the master status to the original node after it comes back up. With this option enabled, if the master status is assigned to a new node it will not be reassigned to the original node for as long as the new master node is running.

```
disable_master_failback=true
```

### `available_when_donor`

This option only has an effect if there is a single Galera node being backed up an XtraBackup instance. This causes the initial node to go into Donor state which would normally prevent if from being marked as a valid server inside MaxScale. If this option is enabled, a single node in Donor state where the method is XtraBackup will be kept in Synced state.

```
available_when_donor=true
```

### `disable_master_role_setting`

This disables the assignment of master and slave roles to the Galera cluster nodes. If this option is enabled, Synced is the only status assigned by this monitor.

```
disable_master_role_setting=true
```

### `use_priority`

Enable interaction with server priorities. This will allow the monitor to deterministically pick the write node for the monitored Galera cluster and will allow for controlled node replacement.

```
use_priority=true
```

### `root_node_as_master`

This option controls whether the write master Galera node requires a
_wsrep_local_index_ value of 0. This option was introduced in MaxScale 2.1.0 and
it is disabled by default in versions 2.1.5 and newer. In versions 2.1.4 and
older, the option was enabled by default.

A Galera cluster will always have a node which has a _wsrep_local_index_ value
of 0. Based on this information, multiple MaxScale instances can always pick the
same node for writes.

If the `root_node_as_master` option is disabled for galeramon, the node with the
lowest index will always be chosen as the master. If it is enabled, only the
node with a a _wsrep_local_index_ value of 0 can be chosen as the master.

### `set_donor_nodes`

This option controls whether the global variable _wsrep_sst_donor_ should be set
in each cluster node with _slave' status_.
The variable contains a list of slave servers, automatically sorted, with
possible master candidates at its end.

The sorting is based either on _wsrep_local_index_ or node server _priority_
depending on the value of _use_priority_ option.
If no server has _priority_ defined the sorting switches to _wsrep_local_index_.
Node names are collected by fetching the result of the variable _wsrep_node_name_.

Example of variable being set in all slave nodes, assuming three nodes:
```
SET GLOBAL wsrep_sst_donor = "galera001,galera000"
```

**Note**:
in order to set the global variable _wsrep_sst_donor_, proper privileges are
required for the monitor user that connects to cluster nodes.
This option is disabled by default and was introduced in MaxScale 2.1.0.

```
set_donor_nodes=true
```

## Interaction with Server Priorities

If the `use_priority` option is set and a server is configured with the
`priority=<int>` parameter, galeramon will use that as the basis on which the
master node is chosen. This requires the `disable_master_role_setting` to be
undefined or disabled. The server with the lowest positive value of _priority_
will be chosen as the master node when a replacement Galera node is promoted to
a master server inside MaxScale.

Nodes with a non-positive value (_priority_ <= 0) will never be chosen as the master. This allows
you to mark some servers as permanent slaves by assigning a non-positive value
into _priority_.

Here is an example.

```
[node-1]
type=server
address=192.168.122.101
port=3306
priority=1

[node-2]
type=server
address=192.168.122.102
port=3306
priority=3

[node-3]
type=server
address=192.168.122.103
port=3306
priority=2

[node-4]
type=server
address=192.168.122.104
port=3306
priority=0
```

In this example `node-1` is always used as the master if available. If `node-1`
is not available, then the next node with the highest priority rank is used. In
this case it would be `node-3`. If both `node-1` and `node-3` were down, then
`node-2` would be used. Because `node-4` has a value of 0 in _priority_, it will
never be the master. Nodes without _priority_ parameter are considered as
having the lowest priority rank and will be used only if all nodes
with _priority_ parameter are not available.

With priority ranks you can control the order in which MaxScale chooses the
master node. This will allow for a controlled failure and replacement of nodes.
