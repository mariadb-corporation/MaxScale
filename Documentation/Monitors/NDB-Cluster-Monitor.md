# NDB Cluster Monitor

**NOTE:** This module has been deprecated, do not use it.

## Overview

The MySQL Cluster Monitor is a monitoring module for MaxScale that monitors a MySQL Cluster. It assigns a NDB status for the server if it is a part of a MySQL Cluster.

## Configuration

A minimal configuration for a monitor requires a set of servers for monitoring and a username and a password to connect to these servers. The user requires the REPLICATION CLIENT privilege to successfully monitor the state of the servers.

```
[MySQL-Cluster-Monitor]
type=monitor
module=ndbclustermon
servers=server1,server2,server3
user=myuser
password=mypwd
```

### Common Monitor Parameters

For a list of optional parameters that all monitors support, read the [Monitor Common](Monitor-Common.md) document.
