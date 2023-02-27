# MariaDB MaxScale 23.02 Release Notes -- 2023-02-27

Release 23.02.0 is a Beta release.

This document describes the changes in release 23.02, when compared to
release 22.08.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

### MariaDBMonitor

MariaDBMonitor now preserves the `MASTER_USE_GTID`-setting of a replica when
redirecting one during switchover and failover. When starting a new
replication connection on a previous replica, `Slave_Pos` is used. When starting
a new replication connection on a previous primary, `Current_Pos` is used.

## Dropped Features

### The `csmon` monitor has been dropped.

Was deprecated in 22.08.2.

### The `auroramon` monitor has been dropped.

### `maxctrl cluster` commands have been removed.

Was deprecated in 22.08.2.

## New Features

### REST-API

#### ODBC Type SQL Connections

The `POST /sql` REST-API endpoint can now use an ODBC driver to connect to an
external data source. To make the use of these new ODBC connections easier, a
new endpoint for canceling active queries was added. The `POST /sql/:id/cancel`
endpoint will interrupt the ongoing operation on the given connection.

For more information on how ODBC type connections differ from native MariaDB
connections, refer to the SQL resource
[documentation](./REST-API/Resources-SQL.md#open-sql-connection-to-server).

#### Asynchronous Query API

The `POST /sql/:id/queries` now supports the `async=true` request option. When
enabled, the results of the query will be delivered asynchronously via the newly
added `GET /sql/:id/queries/:query_id` endpoint.

To make the use of the API easier, the latest asynchronous query result can be
retrieved multiple times. Results can also be explicitly discarded with the new
`DELETE /sql/:id/queries/:query_id` endpoint.

#### [MXS-2709](https://jira.mariadb.org/browse/MXS-2709) ETL/Data Migration Service

The newly added ODBC type connections can be used with the new `/sql/:id/etl`
endpoints to perform data migration operations from external ODBC data sources
into MariaDB. The initial version supports MariaDB-to-MariaDB and
PostgreSQL-to-MariaDB migrations as well as generic migrations done via the ODBC
catalog functions.

For more information on the new API functions, refer to the SQL resource
[documentation](./REST-API/Resources-SQL.md#prepare-etl-operation).

### [MXS-3003](https://jira.mariadb.org/browse/MXS-3003) Support inbound proxy protocol

MaxScale can read an inbound proxy protocol header and relay the information to
backends. See [here](../Getting-Started/Configuration-Guide.md#proxy_protocol_networks)
for more information.

### [MXS-3262](https://jira.mariadb.org/browse/MXS-3262) Add create-backup and restore-from-backup commands to MariaDB-Monitor

These commands backup and restore database contents to/from an external drive.
See [monitor documentation](../Monitors/MariaDB-Monitor.md#backup-operations)
for more information.

### [MXS-3269](https://jira.mariadb.org/browse/MXS-3260) Make it possible to change at runtime the number of threads used by MaxScale

It is now possible to change at runtime the number of threads MaxScale
uses for routing client traffic. See
[here](../Getting-Started/Configuration-Guide.md#threads-1)
for more information.

### [MXS-3708](https://jira.mariadb.org/browse/MXS-3708) Cache runtime modification

Some configuration parameters, most notable the
[rules](../Filters/Cache.md#rules),
can now be changed at runtime.

### [MXS-3827](https://jira.mariadb.org/browse/MXS-3827) Audit log for the REST API

The REST-API calls to MaxScale can now be logged. See
[here](../Getting-Started/Configuration-Guide.md#administration-audit-file)
for more information.

### [MXS-4106](https://jira.mariadb.org/browse/MXS-4106) Redis authentication

Authentication can be enabled when Redis is used as the cache storage. See
[here](../Filters/Cache.md#storage_redis) for more information.

### [MXS-4107](https://jira.mariadb.org/browse/MXS-4107) TLS encrypted Redis connections

SSL/TLS can now be used in the communication between MaxScale and
the Redis server when the latter is used as the storage for the
cache. See
[here](../Filters/Cache.md#storage_redis) for more information.

### [MXS-4182](https://jira.mariadb.org/browse/MXS-4182) Session load indicator

`maxctrl (list|show) sessions` and _MaxGUI_ now show for each session an
_i/o activity_ number that gives an indication of the load of a particular
session. The number is the count of I/O operations performed for the session
during the previous 30 seconds.

### [MXS-4270](https://jira.mariadb.org/browse/MXS-4270) ed25519 authentication support

MariaDB Server ed25519 authentication plugin support added. See
[here](../Authenticators/Ed25519-Authenticator.md) for more information.

### [MXS-4320](https://jira.mariadb.org/browse/MXS-4320) Let maxctrl show datetime values using local client timezone

The maxctrl `list` and `show` commands now display timestamps using the
locale and timezone of the client computer.

### [MXS-4330](https://jira.mariadb.org/browse/MXS-4330) Xpand parallel replication does not work with maxscale


### MaxGUI

Numerous additions have been added and improvements made to MaxGUI.
The most notable ones are listed here:

* [MXS-4013](https://jira.mariadb.org/browse/MXS-4013) Add Views, Functions, and Indexes to the Query Editor schema sidebar.
* [MXS-4285](https://jira.mariadb.org/browse/MXS-4285) Store user preferences.
* [MXS-4430](https://jira.mariadb.org/browse/MXS-4430) ETL/Data Migration service GUI. Instructions on using it can be found [here](../Tutorials/Using-MaxGUI-Tutorial.md#workspace)

## Bug fixes

* [MXS-4490](https://jira.mariadb.org/browse/MXS-4490) Query Editor - A query tab becomes unusable when a connection is not successfully reconnected.

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
