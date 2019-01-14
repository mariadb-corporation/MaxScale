# Clustrix Monitor

## Overview

The Clustrix Monitor is a monitor that monitors a Clustrix cluster. It is
capable of detecting the cluster setup and creating corresponding server
instances within MaxScale.

## Configuration

A minimal configuration for a monitor requires one server in the Clustrix
cluster, and a username and a password to connect to the server. Note that
the Clustrix monitor will only use that server in order to dynamically find
out the configuration of the cluster; after startup it will completely rely
upon information obtained at runtime.

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

### Grants

Note that the monitor user _must_ have `SELECT` grant on the following tables:

   * `system.nodeinfo`
   * `system.membership`

You can give the necessary grants using the following commands:
```
    grant select on system.membership to 'myuser'@'%';
    grant select on system.nodeinfo to 'myuser'@'%';
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
