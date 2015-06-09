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

## Optional parameters for all monitors

Here are optional parameters that are common for all the monitors.

### `monitor_interval`

This is the time the monitor waits between each cycle of monitoring. The default value of 10000 milliseconds (10 seconds) should be lowered if you want a faster response to changes in the server states. The value is defined in milliseconds and the smallest possible value is 100 milliseconds.

```
monitor_interval=2500
```

### `backend_connect_timeout`

This parameter controls the timeout for connecting to a monitored server. It is in seconds and the minimum value is 1 second. The default value for this parameter is 3 seconds.

```
backend_connect_timeout=6
```

### `backend_write_timeout`

This parameter controls the timeout for writing to a monitored server. It is in seconds and the minimum value is 1 second. The default value for this parameter is 2 seconds.

```
backend_write_timeout=4
```

### `backend_read_timeout`

This parameter controls the timeout for reading from a monitored server. It is in seconds and the minimum value is 1 second. The default value for this parameter is 1 seconds.

```
backend_read_timeout=2
```

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
 
### `script`

This script will be executed when a server changes its state. The parameter should be an absolute path to the script or it should be in the executable path.

```
script=/home/user/script.sh
```

### `events`

A list of event names which cause the script to be executed. If this option is not defined, all events cause the script to be executed. The list must contain a comma separated list of event names.

```
events=master_down,slave_down
```

## Script events

Here is a table of all possible event types and their descriptions.

Event Name|Description
----------|----------
master_down|A Master server has gone down
master_up|A Master server has come up
slave_down|A Slave server has gone down
slave_up|A Slave server has come up
server_down|A server with no assigned role has done down
server_up|A server with no assigned role has come up
synced_down|A synced Galera node has come up
synced_up|A synced Galera node has gone down
lost_master|A server lost Master status
lost_slave|A server lost Slave status
lost_synced|A Galera node lost synced status
new_master|A new Master was detected
new_slave|A new Slave was detected
new_synced|A new synced Galera node was detected
