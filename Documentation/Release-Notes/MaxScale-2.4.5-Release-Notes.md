# MariaDB MaxScale 2.4.5 Release Notes

Release 2.4.5 is a GA release.

This document describes the changes in release 2.4.5, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-2785](https://jira.mariadb.org/browse/MXS-2785) Allow database renaming in binlogfilter

## Bug fixes

* [MXS-2809](https://jira.mariadb.org/browse/MXS-2809) 2.4 configuration still contains links to v2.3 documentation
* [MXS-2803](https://jira.mariadb.org/browse/MXS-2803) Hang with readconnroute and persistent connections
* [MXS-2802](https://jira.mariadb.org/browse/MXS-2802) COM_RESET_CONNECTION is treated as a write
* [MXS-2794](https://jira.mariadb.org/browse/MXS-2794) Reloading of users is not logged
* [MXS-2788](https://jira.mariadb.org/browse/MXS-2788) Masking filter performs case-sensitive checks against unquoted case-insensitive identifiers in function calls and WHERE clauses
* [MXS-2782](https://jira.mariadb.org/browse/MXS-2782) Wrong thread id causes MaxScale to crash.
* [MXS-2776](https://jira.mariadb.org/browse/MXS-2776) Binlog filter skipping commit when writing to ColumnStore
* [MXS-2775](https://jira.mariadb.org/browse/MXS-2775) Document that a crashed master can break auto_rejoin with semisynchronous replication

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
