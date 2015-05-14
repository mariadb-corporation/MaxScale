# Multi-Master Monitor

## Overview

The Multi-Master Monitor is a monitoring module for MaxScale that monitors Master-Master replication. It assigns master and slave roles inside MaxScale based on whether the read_only parameter on a server is set to off or on.

## Configuration

A minimal configuration for a monitor requires a set of servers for monitoring and a username and a password to connect to these servers. The user requires the REPLICATION CLIENT privilege to successfully monitor the state of the servers.

```
[Multi-Master Monitor]
type=monitor
module=mmmon
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

## Multi-Master Monitor optional parameters

These are optional parameters specific to the Multi-Master Monitor.

### `detect_stale_master`

Allow previous master to be available even in case of stopped or misconfigured replication. This allows services that depend on master and slave roles to continue functioning as long as the master server is available.

This is a situation which can happen if all slave servers are unreachable or the replication breaks for some reason.

```
detect_stale_master=true
```
 
### `script`

This script will be executed when a server changes its state. The parameter should be an absolute path to the script or it should be in the executable path.

```
script=/home/user/script.sh
```

This script will be called with the following command line arguments.

```
<name of the script> --event=<event type> --initiator=<server whose state changed> --nodelist=<list of all servers>
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
lost_master|A server lost Master status
lost_slave|A server lost Slave status
new_master|A new Master was detected
new_slave|A new Slave was detected
