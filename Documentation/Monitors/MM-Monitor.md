# Multi-Master Monitor

**NOTE:** This module has been deprecated, do not use it.

## Overview

The Multi-Master Monitor is a monitoring module for MaxScale that monitors Master-Master replication.
It assigns master and slave roles inside MaxScale based on whether the read_only parameter on a server
is set to off or on.

## Configuration

A minimal configuration for a monitor requires a set of servers for monitoring and an username
and a password to connect to these servers. The user requires the REPLICATION CLIENT privilege
to successfully monitor the state of the servers.

```
[Multi-Master-Monitor]
type=monitor
module=mmmon
servers=server1,server2,server3
user=myuser
password=mypwd

```

## Common Monitor Parameters

For a list of optional parameters that all monitors support, read
the [Monitor Common](Monitor-Common.md) document.

## Multi-Master Monitor optional parameters

These are optional parameters specific to the Multi-Master Monitor.

### `detect_stale_master`

Allow previous master to be available even in case of stopped or misconfigured replication.
This allows services that depend on master and slave roles to continue functioning as long as
the master server is available.

This is a situation which can happen if all slave servers are unreachable or the
replication breaks for some reason.

```
detect_stale_master=true
```
