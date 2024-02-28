# MariaDB MaxScale 24.02.0 Release Notes -- 2024-02-29

Release 24.02.0 is a Beta release.

This document describes the changes in release 24.02, when compared to
release 23.08.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

### [MXS-4746](https://jira.mariadb.org/browse/MXS-4746) Add last_gtid to session_track_system_variables automatically when causal_reads is enabled

MaxScale now automatically adds *last_gtid* to session-level
*session_track_system_variables* when `causal_reads` is enabled. The setting is
only modified at the start of a backend connection, so clients should not modify
it afterwards.

### [MXS-4748](https://jira.mariadb.org/browse/MXS-4748) Support non-default datadir with rebuild-server

`async-rebuild-server` and `async-restore-from-backup` now auto-detect server
data directory. Alternatively, the data directory can be specified manually when
launching the operations from command line. Also, Mariabackup memory use and
thread count can be customized in monitor settings. See
[MariaDB Monitor documentation](../Monitors/MariaDB-Monitor.md#backup_operations)
for more information.

## Dropped Features

### Legacy SysV init scripts

The legacy SysV init scripts as well as the Upstart files have been
removed. These were never used by MaxScale if it was installed from a RPM or DEB
package.

## Deprecated Features

## New Features

### [MXS-3616](https://jira.mariadb.org/browse/MXS-3616) Support MARIADB_CLIENT_EXTENDED_TYPE_INFO

MaxScale now supports the extended result type information extension to the
MariaDB network protocol. This extension to the protocol was added in MariaDB
10.5 and some MariaDB connectors like Connector/J benefit from it.

### [MXS-3986](https://jira.mariadb.org/browse/MXS-3986) Binlog compression and archiving

Binlog files can now be automatically compressed.
An option to purging binlogs is now to archive them to another file system or for example, to Amazon S3.

[Binlogrouter](../Routers/Binlogrouter.md#binlog_purge_archive_and_compress)

### [MXS-4191](https://jira.mariadb.org/browse/MXS-4191) Restrict REST API user authentication to specific IPs

Global settings `admin_readwrite_hosts` and `admin_readonly_hosts` limit the
hostnames/IPs from which admin (REST-API) clients can log in from. See
[admin_readonly_hosts](../Getting-Started/Configuration-Guide.md#admin_readonly_hosts) and
[admin_readwrite_hosts](../Getting-Started/Configuration-Guide.md#admin_readwrite_hosts)
for more information.

### [MXS-4705](https://jira.mariadb.org/browse/MXS-4705) Support multiple IPs for one server

`private_address` is an alternative IP-address or hostname for a server. This is
used by MariaDB Monitor to detect and set up replication. See
[MariaDB Monitor documentation](../Monitors/MariaDB-Monitor.md#private_address)
for more information.

### [MXS-4764](https://jira.mariadb.org/browse/MXS-4764) KafkaCDC: Option to use the latest GTID

The [gtid](../Routers/KafkaCDC.md#gtid) parameter now supports the special
values `newest` that uses the value of `@@gtid_binlog_pos` and `oldest` that
scans the output of `SHOW BINLOG EVENTS` for the earliest GTID.


### MaxGUI
Numerous additions have been added and improvements made to MaxGUI.
The most notable ones are listed here:

### [MXS-3620](https://jira.mariadb.org/browse/MXS-3620) Filter log data by module, object, session id on the GUI

### [MXS-3851](https://jira.mariadb.org/browse/MXS-3851) Show more KafkaCDC router statistics on GUI

### [MXS-3919](https://jira.mariadb.org/browse/MXS-3919) Add `interactive_timeout` and `wait_timeout` inputs in the Query Editor settings

### [MXS-4017](https://jira.mariadb.org/browse/MXS-4017) Query Editor Auto completion for all identifier names of the active schema

### [MXS-4143](https://jira.mariadb.org/browse/MXS-4143) Able to export columns data with table structure for only those selected columns like sqlYog

### [MXS-4375](https://jira.mariadb.org/browse/MXS-4375) Logs Archive Filter Between Dates

### [MXS-4466](https://jira.mariadb.org/browse/MXS-4466) Greater detail / customization of Maxscale GUI Dashboard Load

### [MXS-4447](https://jira.mariadb.org/browse/MXS-4447) Show column definition in the Query Editor

### [MXS-4535](https://jira.mariadb.org/browse/MXS-4535) Connection resource type user preference

### [MXS-4572](https://jira.mariadb.org/browse/MXS-4572) Improve UX Setting up with GUI

### [MXS-4770](https://jira.mariadb.org/browse/MXS-4770) Show config sync information in the GUI

## Bug fixes

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the supported Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/#mariadb_platform-mariadb_maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
