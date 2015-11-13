# NDB Cluster Monitor

## Overview

The MySQL Cluster Monitor is a monitoring module for MaxScale that monitors a MySQL Cluster. It assigns a NDB status for the server if it is a part of a MySQL Cluster.

## Configuration

A minimal configuration for a monitor requires a set of servers for monitoring and a username and a password to connect to these servers. The user requires the REPLICATION CLIENT privilege to successfully monitor the state of the servers.

```
[MySQL Cluster Monitor]
type=monitor
module=ndbclustermon
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

### Common Monitor Parameters

For a list of optional parameters that all monitors support, read the [Monitor Common](Monitor-Common.md) document.
