# Galera Monitor

[TOC]

## Overview

The Galera Monitor is a monitoring module for MaxScale that monitors a Galera
cluster. It detects whether nodes are a part of the cluster and if they are in
sync with the rest of the cluster. It can also assign primary and replica roles
inside MaxScale, allowing Galera clusters to be used with modules designed for
traditional primary-replica clusters.

By default, the Galera Monitor will choose the node with the lowest
`wsrep_local_index` value as the primary. This will mean that two MaxScales
running on different servers will choose the same server as the primary.

### Galera clusters and replicas replicating from it

MaxScale 2.4.0 added support for replicas replicating off of Galera nodes. If a
non-Galera server monitored by galeramon is replicating from a Galera node also
monitored by galeramon, it will be assigned the `Slave, Running` status as long
as the replication works. This allows read-scaleout with Galera servers without
increasing the size of the Galera cluster.

## Required Grants

The Galera Monitor requires the `REPLICATION CLIENT` grant to work:

```
CREATE USER 'maxscale'@'maxscalehost' IDENTIFIED BY 'maxscale-password';
GRANT REPLICATION CLIENT ON *.* TO 'maxscale-user'@'maxscalehost';
```

if `set_donor_nodes` is configured, the `SUPER` grant is required:

```
GRANT SUPER ON *.* TO 'maxscale'@'maxscalehost';
```

## Configuration

A minimal configuration for a monitor requires a set of servers for monitoring
and a username and a password to connect to these servers. The user requires the
REPLICATION CLIENT privilege to successfully monitor the state of the servers.

```
[Galera-Monitor]
type=monitor
module=galeramon
servers=server1,server2,server3
user=myuser
password=mypwd

```

## Common Monitor Parameters

For a list of optional parameters that all monitors support, read the
[Monitor Common](Monitor-Common.md) document.

## Galera Monitor optional parameters

These are optional parameters specific to the Galera Monitor.

### `disable_master_failback`

- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

If a node marked as primary inside MaxScale happens to fail and the primary
status is assigned to another node MaxScale will normally return the primary
status to the original node after it comes back up. With this option enabled, if
the primary status is assigned to a new node it will not be reassigned to the
original node for as long as the new primary node is running. In this case the
`Master Stickiness` status bit is set which will be visible in the
`maxctrl list servers` output.

### `available_when_donor`

- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

This option allows Galera nodes to be used normally when they are donors in an
SST operation when the SST method is non-blocking
(e.g. `wsrep_sst_method=mariabackup`).

Normally when an SST is performed, both participating nodes lose their `Synced`,
`Master` or `Slave` statuses. When this option is enabled, the donor is treated as
if it was a normal member of the cluster (i.e. `wsrep_local_state = 4`). This is
especially useful if the cluster drops down to one node and an SST is required
to increase the cluster size.

The current list of non-blocking SST
methods are `xtrabackup`, `xtrabackup-v2` and `mariabackup`. Read the
[wsrep_sst_method](https://mariadb.com/kb/en/library/galera-cluster-system-variables/#wsrep_sst_method)
documentation for more details.

### `disable_master_role_setting`

- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

This disables the assignment of primary and replica roles to the Galera cluster
nodes. If this option is enabled, Synced is the only status assigned by this
monitor.

### `use_priority`

- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

Enable interaction with server priorities. This will allow the monitor to
deterministically pick the write node for the monitored Galera cluster and will
allow for controlled node replacement.

### `root_node_as_master`

- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

This option controls whether the write primary Galera node requires a
_wsrep_local_index_ value of 0. This option was introduced in MaxScale 2.1.0 and
it is disabled by default in versions 2.1.5 and newer. In versions 2.1.4 and
older, the option was enabled by default.

A Galera cluster will always have a node which has a _wsrep_local_index_ value
of 0. Based on this information, multiple MaxScale instances can always pick the
same node for writes.

If the `root_node_as_master` option is disabled for galeramon, the node with the
lowest index will always be chosen as the primary. If it is enabled, only the
node with a a _wsrep_local_index_ value of 0 can be chosen as the primary.

This parameter can work with `disable_master_failback` but using them together
is not advisable: the intention of `root_node_as_master` is to make sure that
all MaxScale instances that are configured to use the same Galera cluster will
send writes to the same node. If `disable_master_failback` is enabled, this is
no longer true if the Galera cluster reorganizes itself in a way that a
different node gets the node index 0, writes would still be going to the old
node that previously had the node index 0. A restart of one of the MaxScales or
a new MaxScale joining the cluster will cause writes to be sent to the wrong
node, thus resulting in an increasing the rate of deadlock errors and
sub-optimal performance.

### `set_donor_nodes`

- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

This option controls whether the global variable _wsrep_sst_donor_ should be set
in each cluster node with _slave' status_.
The variable contains a list of replica servers, automatically sorted, with
possible primary candidates at its end.

The sorting is based either on _wsrep_local_index_ or node server _priority_
depending on the value of _use_priority_ option.
If no server has _priority_ defined the sorting switches to _wsrep_local_index_.
Node names are collected by fetching the result of the variable _wsrep_node_name_.

Example of variable being set in all replica nodes, assuming three nodes:
```
SET GLOBAL wsrep_sst_donor = "galera001,galera000"
```

**Note**:
in order to set the global variable _wsrep_sst_donor_, proper privileges are
required for the monitor user that connects to cluster nodes.
This option is disabled by default and was introduced in MaxScale 2.1.0.

## Interaction with Server Priorities

If the `use_priority` option is set and a server is configured with the
`priority=<int>` parameter, galeramon will use that as the basis on which the
primary node is chosen. This requires the `disable_master_role_setting` to be
undefined or disabled. The server with the lowest positive value of _priority_
will be chosen as the primary node when a replacement Galera node is promoted to
a primary server inside MaxScale. If all candidate servers have the same
priority, the order of the servers in the `servers` parameter dictates which is
chosen as the primary.

Nodes with a negative value (_priority_ < 0) will never be chosen as the
primary. This allows you to mark some servers as permanent replicas by assigning a
non-positive value into _priority_. Nodes with the default priority of 0 are
only selected if no nodes with higher priority are present and the normal node
selection rules apply to them (i.e. selection is based on `wsrep_local_index`).

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
priority=-1
```

In this example `node-1` is always used as the primary if available. If `node-1`
is not available, then the next node with the highest priority rank is used. In
this case it would be `node-3`. If both `node-1` and `node-3` were down, then
`node-2` would be used. Because `node-4` has a value of -1 in _priority_, it
will never be the primary. Nodes without _priority_ parameter are considered as
having a priority of 0 and will be used only if all nodes with a positive
_priority_ value are not available.

With priority ranks you can control the order in which MaxScale chooses the
primary node. This will allow for a controlled failure and replacement of nodes.
