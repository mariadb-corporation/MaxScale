# MariaDB MaxScale 22.8 Release Notes

The versioning scheme for MaxScale releases has changed; the format of the
version will be `YY.MM.PATCH` where `YY` is the last two digits of the year and
`MM` is the month when the release was made. The `PATCH` is a number that is
incremented whenever a maintenance release is made.

According to the old scheme, this MaxScale release would have been called 7 and
the version number would have been 7.0.0.

Release 22.8.0 is a Beta release.

This document describes the changes in release 22.8, when compared to
release 6.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

### [MXS-2724](https://jira.mariadb.org/browse/MXS-2724) Expose memory usage information

`maxctrl [list|show] session(s)` now provides information about
the memory usage of a session.

### [MXS-2882](https://jira.mariadb.org/browse/MXS-2882) Query Log All Filter

The `log_data` support a new value `server` that causes the server
where a query was routed to be logged.

### [MXS-3490](https://jira.mariadb.org/browse/MXS-3490) Xpand Group Change

The Xpand monitor now handles group change explicitly, which leads
to more robust behavior and less warnings/errors logged.

### [MXS-3605](https://jira.mariadb.org/browse/MXS-3605) Maximum number of routing threads

The maximum number of routing threads has been increased to 256 (was 100).

### [MXS-3619](https://jira.mariadb.org/browse/MXS-3619) Synchronizion of server states

When configuration synchronization is enabled, if the `Maintenance`
or `Drain` state of a server is changed, it will affect all MaxScale
instances. Earlier the state was local to a particular MaxScale instance.

### [MXS-3663](https://jira.mariadb.org/browse/MXS-3663) Causal Reads

Causal reads are now supported also in a multi-MaxScale environment. For
more information, please see [causal_reads](../Routers/ReadWriteSplit.md#causal_reads).

### [MXS-3912](https://jira.mariadb.org/browse/MXS-3912) maxctrl list users

Now shows the last login of a user.

### [MXS-4067](https://jira.mariadb.org/browse/MXS-4067) Proxy Protocol

If _proxy protocol_ is enabled, then MaxScale will use the proxy
protocol also when it internally opens a connection to a server.

### [MXS-4145](https://jira.mariadb.org/browse/MXS-3145) Multi-MaxScale nosqlprotocol usage

It is now possible to use the nosqlprotocol protocol module also in a
multi-MaxScale setup. Please see
[NoSQL Account Database](../Protocols/NoSQL.md#nosql-account-database)
for more information.

### [MXS-4192](https://jira.mariadb.org/browse/MXS-4192) Logging default

MaxScale no longer _also_ logs to syslog by default. Specify `syslog=true`
under the `[maxscale]` section to retain the old behavior.

## Dropped Features

### MariaDB Monitor

MariaDB-Monitor settings `ignore_external_masters`, `detect_replication_lag`
`detect_standalone_master`, `detect_stale_master` and `detect_stale_slave`
have been removed. The first two were ineffective, the latter three are
replaced by `master_conditions` and `slave_conditions`.

### REST API

The `/v1/maxscale/tasks/` endpoint has been removed from the REST-API.

### Database Firewall Filter

The `dbfwfilter` that was deprecated in MaxScale 6 has been removed in
MaxScale 22.8.

## Deprecated Features

### `ssl_ca_cert`

The server parameter `ssl_ca_cert` has been renamed to `ssl_ca` and
`ssl_ca_cert` has been deprecated. `ssl_ca_cert` is now an alias for
`ssl_ca` and can still be used, but we suggest taking `ssl_ca` into
use, as the support for `ssl_ca_cert` will at some point be dropped.

### `admin_ssl_ca_cert`

The server parameter `admin_ssl_ca_cert` has been renamed to `admin_ssl_ca`
and `admin_ssl_ca_cert` has been deprecated. `admin_ssl_ca_cert` is now an
alias for `admin_ssl_ca` and can still be used, but we suggest taking
`admin_ssl_ca` into use, as the support for `admin_ssl_ca_cert` will at
some point be dropped.

## New Features

### [MXS-2347](https://jira.mariadb.org/browse/MXS-2347) Session restarting

Sessions can now be restarted, which will cause servers that have
been added since the session was started to be taken into use.

### [MXS-2542](https://jira.mariadb.org/browse/MXS-2542) Rebuild server

MariaDBMonitor can use Mariabackup to
[clone](../Monitors/MariaDB-Monitor.md#rebuild-server) the contents of a server.

### [MXS-3152](https://jira.mariadb.org/browse/MXS-3952) Kill session

Now possible to kill a session using _maxctrl_.

### [MXS-3217](https://jira.mariadb.org/browse/MXS-3217) ColumnStore commands

MariaDBMonitor can issue
[ColumnStore commands](../Monitors/MariaDB-Monitor.md#columnstore-commands)
similar to CSMon.

### [MXS-3398](https://jira.mariadb.org/browse/MXS-3398)  Auto tuning of configuration parameters

MaxScale is now capable of autonomously setting the value of some
configuration parameters based upon the value of a configuration
parameter of the server. For more information, please refer to
[auto_tune](../Getting-Started/Configuration-Guide.md#auto_tune).

### [MXS-3982](https://jira.mariadb.org/browse/MXS-3982) TLS Certificate Reloading

Now possible to reload the TLS certificates of servers and listeners using _maxctrl_.

### [MXS-4010](https://jira.mariadb.org/browse/MXS-4010) Purging Avro log files

The avro router is now capable of purging old log files. For more
information, please see [max_data_age](../Routers/Avrorouter.md#max_data_age).

### [MXS-4041](https://jira.mariadb.org/browse/MXS-4041) REST API TSL Certificate Reloading

Note possible to reload the TLS certificats of the REST-API using _maxctrl_.

### [MXS-4052](https://jira.mariadb.org/browse/MXS-4052) Kafka Schema object

The sending of the JSON schema objects can now be disabled. For
more information please see [send_schema](../Routers/KafkaCDC.md#send_schema).

### MaxGUI

Numerous additions have been added and improvements made to MaxGUI.
The most notable ones are listed here:

[MXS-3216](https://jira.mariadb.org/browse/MXS-3216) Add Columnstore operations to MaxGUI
[MXS-3642](https://jira.mariadb.org/browse/MXS-3642) Control replication with drag and drop in MaxGUI
[MXS-3723](https://jira.mariadb.org/browse/MXS-3723) Save and Load .sql files in the Query Editor
[MXS-3725](https://jira.mariadb.org/browse/MXS-3725) Allow storing query as snippets in the Query Editor
[MXS-3783](https://jira.mariadb.org/browse/MXS-3783) User access control in MaxGUI
[MXS-3853](https://jira.mariadb.org/browse/MXS-3853) Manage MaxScale users in MaxGUI
[MXS-3918](https://jira.mariadb.org/browse/MXS-3918) Add stop query button in the Query Editor
[MXS-4025](https://jira.mariadb.org/browse/MXS-4025) Multiple query tabs on the same worksheet
[MXS-4087](https://jira.mariadb.org/browse/MXS-4087) Kill session in MaxGUI
[MXS-4167](https://jira.mariadb.org/browse/MXS-4167) Show filter diagnostics in MaxGUI

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
