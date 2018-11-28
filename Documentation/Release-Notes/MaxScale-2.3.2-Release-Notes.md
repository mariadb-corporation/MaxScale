# MariaDB MaxScale 2.3.2 Release Notes

Release 2.3.2 is a GA release.

This document describes the changes in release 2.3.2, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

### Watchdog

The systemd watchdog is now safe to use in all circumstances.

By default it is enabled with a timeout of 60 seconds.

### Readwritesplit

#### `connection_keepalive`

The default value of `connection_keepalive` is now 300 seconds. This prevents
the connections from dying due to wait_timeout with longer sessions. This is
especially helpful with pooled connections that stay alive for a very long time.

### MariaDBMonitor

The monitor by default assumes that hostnames used by MaxScale to connect to the backends
are equal to the ones backends use to connect to each other. Specifically, for the slave
connections to be properly detected the `Master_Host` and `Master_Port` fields of the
output to "SHOW ALL SLAVES STATUS"-query must match server entries in the MaxScale
configuration file. If the network configuration is such that this is not the case, the
setting `assume_unique_hostnames` should be disabled.

## New Features

* [MXS-1598](https://jira.mariadb.org/browse/MXS-1598) heartbeat replication don't support multimaster

## Bug fixes

* [MXS-2189](https://jira.mariadb.org/browse/MXS-2189) optimistic_trx is rolled back if master fails
* [MXS-2187](https://jira.mariadb.org/browse/MXS-2187) Transaction replay is only attempted once
* [MXS-2186](https://jira.mariadb.org/browse/MXS-2186) SHOW DATABASES is routed to the master
* [MXS-2184](https://jira.mariadb.org/browse/MXS-2184) event_number is not incremented for updates
* [MXS-2179](https://jira.mariadb.org/browse/MXS-2179) Watchdog notifications must be generated also when users are fetched.
* [MXS-2178](https://jira.mariadb.org/browse/MXS-2178) Admin operations may cause systemd watchdog to be triggered.
* [MXS-2167](https://jira.mariadb.org/browse/MXS-2167) Monitors should be able to use extra_port
* [MXS-2158](https://jira.mariadb.org/browse/MXS-2158) Node rejoin fails, if the node was never a slave (but was master before going down)

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for supported the Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/mariadb-tx/maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
