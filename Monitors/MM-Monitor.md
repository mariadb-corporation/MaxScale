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

### Common Monitor Parameters

For a list of optional parameters that all monitors support, read the [Monitor Common](Monitor-Common.md) document.
