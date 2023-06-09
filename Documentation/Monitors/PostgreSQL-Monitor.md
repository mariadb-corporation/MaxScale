# PostgreSQL Monitor

[TOC]

## Overview

PostgreSQL Monitor monitors PostgreSQL servers. It probes the state of the
servers, detecting if they are running or not. The monitor is very simple and
does not detect replication.

## Required Grants

The pg_hba.conf-file should have a line that allows the monitor user (e.g.
"maxmon") to log in to database "postgres". Change the ip to match the
MaxScale host ip or use "all".
```
host    postgres     maxmon    127.0.0.1/32      scram-sha-256
```
The monitor user does not require any special grants.
```
create role maxmon login password 'maxpw';
```

## Configuration

A minimal configuration is below. *type*, *module*, monitored servers and login
credentials are mandatory.

```
[PG-Monitor]
type=monitor
module=pgmon
servers=PGServer1
user=maxmon
password=maxpw
```

For a list of optional parameters that all monitors support, read the
[Monitor Common](Monitor-Common.md) document. PostgreSQL Monitor does
not require any monitor-specific additional parameters.
