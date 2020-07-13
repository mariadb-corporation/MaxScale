# MariaDB MaxScale 2.4.11 Release Notes -- 2020-07-13

Release 2.4.11 is a GA release.

This document describes the changes in release 2.4.11, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3069](https://jira.mariadb.org/browse/MXS-3069) Unnecessary AuthSwitchRequest with old clients
* [MXS-3064](https://jira.mariadb.org/browse/MXS-3064) User loading query can run into charset collation problems
* [MXS-3059](https://jira.mariadb.org/browse/MXS-3059) Crash in Galera monitor
* [MXS-3057](https://jira.mariadb.org/browse/MXS-3057) session_trace enables all log levels
* [MXS-3055](https://jira.mariadb.org/browse/MXS-3055) DCB write errors with abstracted services
* [MXS-3054](https://jira.mariadb.org/browse/MXS-3054) Crash with persistent connections, schemarouter and local services abstracted as servers
* [MXS-3041](https://jira.mariadb.org/browse/MXS-3041) QC bug with keyword handler
* [MXS-3038](https://jira.mariadb.org/browse/MXS-3038) readwritesplit router should not open new connections to slaves lagging for more than max_slave_replication_lag seconds
* [MXS-3020](https://jira.mariadb.org/browse/MXS-3020) Parts of 2.4 manual still suggest to use deprecated maxadmin
* [MXS-2996](https://jira.mariadb.org/browse/MXS-2996) Query Type of 'SELECT ... LOCK IN SHARE MODE;'
* [MXS-2585](https://jira.mariadb.org/browse/MXS-2585) MaxScale dies with fatal signal 11
* [MXS-2584](https://jira.mariadb.org/browse/MXS-2584) Race condition between startup/shutdown and signal delivery
* [MXS-619](https://jira.mariadb.org/browse/MXS-619) creating many short sessions in parallel leads to errors

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for supported the Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/#mariadb_platform-mariadb_maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
