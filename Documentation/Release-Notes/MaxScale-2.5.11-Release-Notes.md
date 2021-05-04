# MariaDB MaxScale 2.5.11 Release Notes -- 2021-05-04

Release 2.5.11 is a GA release.

This document describes the changes in release 2.5.11, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3509](https://jira.mariadb.org/browse/MXS-3509) Syslog output is not filtered according to current log configuration
* [MXS-3505](https://jira.mariadb.org/browse/MXS-3505) option lazy_connect not fully effective
* [MXS-3504](https://jira.mariadb.org/browse/MXS-3504) Kafkacdc doesn't reconnect on replication error
* [MXS-3487](https://jira.mariadb.org/browse/MXS-3487) Old master connection is left open after transaction migration
* [MXS-3483](https://jira.mariadb.org/browse/MXS-3483) MaxCtrl doesn't strip colors from non-tty output
* [MXS-3472](https://jira.mariadb.org/browse/MXS-3472) Transaction Replay: transactions not replayed after Xpand group change
* [MXS-3471](https://jira.mariadb.org/browse/MXS-3471) After "clock has been changed to REALTIME" in syslog maxscale crashes
* [MXS-3468](https://jira.mariadb.org/browse/MXS-3468) Read statistics are wrong with master_accept_reads and read-only transactions
* [MXS-3462](https://jira.mariadb.org/browse/MXS-3462) Updates to services don't propagate upwards to other services that use them
* [MXS-3459](https://jira.mariadb.org/browse/MXS-3459) Malformed packet SQL=LOAD DATA LOCAL INFILE... ERROR 2027
* [MXS-3454](https://jira.mariadb.org/browse/MXS-3454) Prepared statement inside trx not tracked with transaction_replay

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
