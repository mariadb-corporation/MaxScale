# MySQL Replication Monitor

[TOC]

## Overview

MySQL Replication Monitor (`mysqlrepmon`) monitors a MySQL 8.x
Primary/Replica replication cluster and can perform automatic **failover**,
**switchover** and **rejoin** using MySQL's UUID-based GTIDs
(`SOURCE_AUTO_POSITION = 1`).

It is a MySQL-specific sibling of [MariaDB Monitor](MariaDB-Monitor.md).
Where MariaDB Monitor speaks MariaDB's replication dialect
(`SHOW ALL SLAVES STATUS`, `domain-server-sequence` GTIDs, `CHANGE MASTER
TO ... MASTER_USE_GTID`), `mysqlrepmon` speaks the MySQL 8.x dialect that
MySQL 8.4 now requires:

* `SHOW REPLICA STATUS` (the legacy `SHOW SLAVE STATUS` was removed in 8.4)
* `CHANGE REPLICATION SOURCE TO ... SOURCE_AUTO_POSITION = 1 ... FOR CHANNEL`
* `START` / `STOP` / `RESET REPLICA ... FOR CHANNEL`
* `SET GLOBAL super_read_only = 1` to demote a primary
* GTID state read from `@@global.gtid_executed` / `@@global.gtid_purged`

The configuration parameters, monitor commands (`switchover`, `failover`,
`rejoin`, `reset-replication`, and their `async-` variants) and the
failover/switchover/rejoin behaviour are identical to MariaDB Monitor; see
that document for the full parameter reference. Use `module=mysqlrepmon`
instead of `module=mariadbmon`.

## Requirements

* MySQL Server 8.0.22 or later on all backends (8.4 supported).
* `gtid_mode = ON` and `enforce_gtid_consistency = ON` on every server, with
  replicas configured for GTID auto-positioning
  (`CHANGE REPLICATION SOURCE TO SOURCE_AUTO_POSITION = 1`).
* `log_bin` and `log_replica_updates` enabled on servers that may be promoted.

## Required Grants

For monitoring only:
```
CREATE USER 'maxscale'@'maxscalehost' IDENTIFIED BY 'maxscale-password';
GRANT REPLICATION CLIENT ON *.* TO 'maxscale'@'maxscalehost';
```

For failover / switchover / rejoin the monitor user additionally needs the
privileges to reconfigure replication and toggle read-only, e.g.:
```
GRANT REPLICATION SLAVE, REPLICATION CLIENT, PROCESS, SUPER,
      SYSTEM_VARIABLES_ADMIN, REPLICATION_SLAVE_ADMIN, CONNECTION_ADMIN
      ON *.* TO 'maxscale'@'maxscalehost';
```
The `replication_user` used in the generated `CHANGE REPLICATION SOURCE`
command must have `REPLICATION SLAVE` on all servers.

When the backends use the default `caching_sha2_password` authentication over
a non-TLS connection, the monitor issues the `CHANGE REPLICATION SOURCE`
command with `GET_SOURCE_PUBLIC_KEY = 1` so the replica can complete the
handshake with the promoted primary. Configure `replication_master_ssl=true`
to use TLS instead.

## Example configuration

```
[MySQL-Monitor]
type=monitor
module=mysqlrepmon
servers=server1,server2,server3
user=maxscale
password=maxscale-password
monitor_interval=2000ms
auto_failover=true
auto_rejoin=true
enforce_read_only_slaves=true
replication_user=maxscale
replication_password=maxscale-password
```

## Implementation notes

Internally the monitor maps each MySQL `server_uuid` to a stable synthetic
GTID *domain* and reduces each GTID interval set to its highest transaction
number. This lets the shared MariaDB Monitor failover engine reason about
"which replica is furthest ahead" and "can A replicate from B" without any
change to that logic. MySQL's `Retrieved_Gtid_Set` maps to the engine's
relay-log GTID position and `gtid_executed` to the applied position.
