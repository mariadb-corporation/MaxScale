# MariaDB MaxScale 6.2.3 Release Notes

Release 6.2.3 is a GA release.

This document describes the changes in release 6.2.3, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.2.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4038](https://jira.mariadb.org/browse/MXS-4038) maxctrl reload service does not bypass the users refresh rate limit
* [MXS-4035](https://jira.mariadb.org/browse/MXS-4035) Cache warns too aggressively of statements that cannot be cached.
* [MXS-4030](https://jira.mariadb.org/browse/MXS-4030) Query Editor: Y axis dropdown doesn't show accurate table columns
* [MXS-4021](https://jira.mariadb.org/browse/MXS-4021) Monitor is not shown in MaxGUI's dashboard if the monitor is stopped
* [MXS-4011](https://jira.mariadb.org/browse/MXS-4011) maxscale.cnf.template on MaxScale 6.x refers to 2.5 documentation
* [MXS-4008](https://jira.mariadb.org/browse/MXS-4008) Query classifier cache does not properly record all used memory
* [MXS-4007](https://jira.mariadb.org/browse/MXS-4007) Active operation count is wrong after failed causal read
* [MXS-4005](https://jira.mariadb.org/browse/MXS-4005) Crash on server failure with causal_reads=local
* [MXS-4004](https://jira.mariadb.org/browse/MXS-4004) Race condition in KILL command execution
* [MXS-4002](https://jira.mariadb.org/browse/MXS-4002) KILL commands leave no trace in the log
* [MXS-4001](https://jira.mariadb.org/browse/MXS-4001) The Cache filter cannot cope with the Redis server closing the connection
* [MXS-4000](https://jira.mariadb.org/browse/MXS-4000) Binlogrouter creates malformed replication events
* [MXS-3988](https://jira.mariadb.org/browse/MXS-3988) Document implications of changed auth_all_servers default on schemarouter
* [MXS-3984](https://jira.mariadb.org/browse/MXS-3984) COM_CHANGE_USER from 'user' to 'user' succeeded on MaxScale yet failed on backends
* [MXS-3979](https://jira.mariadb.org/browse/MXS-3979) Not all state transitions are written to the log
* [MXS-3957](https://jira.mariadb.org/browse/MXS-3957) Remove the `Don't Limit` option for max_rows value of the Query Editor
* [MXS-3954](https://jira.mariadb.org/browse/MXS-3954) Got below signal 11 error after upgrading maxscale version  maxscale 6.2.1
* [MXS-3945](https://jira.mariadb.org/browse/MXS-3945) Sync marker mismatch while reading Avro file
* [MXS-3931](https://jira.mariadb.org/browse/MXS-3931) Check certificates with extendedKeyUsage options set for correct purpose flags
* [MXS-3808](https://jira.mariadb.org/browse/MXS-3808) Improve Rest API performance

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
