# MariaDB MaxScale 24.02.2 Release Notes -- 2024-06-03

Release 24.02.2 is a GA release.

This document describes the changes in release 24.02.2, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-24.02.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-5067](https://jira.mariadb.org/browse/MXS-5067) Add "enforce_read_only_servers" feature to MariaDB Monitor

## Bug fixes

* [MXS-5106](https://jira.mariadb.org/browse/MXS-5106) Server version checks are overly pessimistic
* [MXS-5104](https://jira.mariadb.org/browse/MXS-5104) Connection busy error occurs when connecting to a listener in the Query Editor
* [MXS-5101](https://jira.mariadb.org/browse/MXS-5101) MariaDB Monitor can kill connections from other monitors during switchover
* [MXS-5095](https://jira.mariadb.org/browse/MXS-5095) Master Stickiness state is not documented
* [MXS-5094](https://jira.mariadb.org/browse/MXS-5094) Stacktraces fail to be generated when MaxScale is run from the terminal
* [MXS-5093](https://jira.mariadb.org/browse/MXS-5093) SQL API does not return binary data in resultsets
* [MXS-5091](https://jira.mariadb.org/browse/MXS-5091) admin_audit file name does not use log_dir value
* [MXS-5090](https://jira.mariadb.org/browse/MXS-5090) ability to setup .secrets file location
* [MXS-5085](https://jira.mariadb.org/browse/MXS-5085) max_slave_connections=0 may create slave connections after a switchover
* [MXS-5083](https://jira.mariadb.org/browse/MXS-5083) ssl_version in MaxScale and tls_version in MariaDB behave differently
* [MXS-5082](https://jira.mariadb.org/browse/MXS-5082) Password encryption format change in 2.5 is not documented very well
* [MXS-5081](https://jira.mariadb.org/browse/MXS-5081) The values of ssl_version in MaxScale and tls_version in MariaDB accept different values
* [MXS-5074](https://jira.mariadb.org/browse/MXS-5074) Warning about missing slashes around regular expressions is confusing
* [MXS-5068](https://jira.mariadb.org/browse/MXS-5068) users_refresh_time=0s does not work as documented
* [MXS-5063](https://jira.mariadb.org/browse/MXS-5063) Maxscale crash - "terminate called after throwing an instance of 'std::bad_alloc'"
* [MXS-5051](https://jira.mariadb.org/browse/MXS-5051) cmake does not check for unixodbc-dev
* [MXS-5048](https://jira.mariadb.org/browse/MXS-5048) Problem in hostname matching when using regex (%) for user authentication
* [MXS-5039](https://jira.mariadb.org/browse/MXS-5039) cooperative_monitoring_locks can leave stale locks on a server if network breaks
* [MXS-5038](https://jira.mariadb.org/browse/MXS-5038) Maxscale key limitations
* [MXS-5023](https://jira.mariadb.org/browse/MXS-5023) kill user and transaction_replay don't play well together in Galera cluster
* [MXS-5021](https://jira.mariadb.org/browse/MXS-5021) gdb-stacktrace is incorrectly presented as a debug option
* [MXS-4964](https://jira.mariadb.org/browse/MXS-4964) Simple sharding tutorial is out of date
* [MXS-4902](https://jira.mariadb.org/browse/MXS-4902) MariaDB Monitor command reset-replication can be started on a secondary MaxScale

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
