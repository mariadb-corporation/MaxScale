# Galera Monitor

## Overview

The Galera Monitor is a monitoring module for MaxScale that monitors a Galera cluster. It detects whether nodes are a part of the cluster and if they are in sync with the rest of the cluster. It can also assign master and slave roles inside MaxScale, allowing Galera clusters to be used with modules designed for traditional master-slave clusters.

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

## Interaction with Server Priorities

If the `use_priority` option is set and a server is configured with the `priority=<int>` parameter, galeramon will use that as the basis on which the master node is chosen. This requires the `disable_master_role_setting` to be undefined or disabled. The server with the lowest value in `priority` will be chosen as the master node when a replacement Galera node is promoted to a master server inside MaxScale.

Here is an example with two servers.

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
```

In this example `node-1` is always used as the master if available. If `node-1` is not available, then the next node with the highest priority rank is used. In this case it would be `node-3`. If both `node-1` and `node-3` were down, then `node-2` would be used. Nodes without priority are considered as having the lowest priority rank and will be used only if all nodes with priority ranks are not available.

With priority ranks you can control the order in which MaxScale chooses the master node. This will allow for a controlled failure and replacement of nodes.
