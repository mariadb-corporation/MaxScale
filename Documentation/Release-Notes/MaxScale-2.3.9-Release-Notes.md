# MariaDB MaxScale 2.3.9 Release Notes -- 2019-07-04

Release 2.3.9 is a GA release.

This document describes the changes in release 2.3.9, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-2480](https://jira.mariadb.org/browse/MXS-2480) Write paths to opened sqlite3 databases in log when log_info is set

## Bug fixes

* [MXS-2582](https://jira.mariadb.org/browse/MXS-2582) Intermittent unknown statement handler errors from backends
* [MXS-2578](https://jira.mariadb.org/browse/MXS-2578) Maxscale RPM issue PCI Compliancy
* [MXS-2575](https://jira.mariadb.org/browse/MXS-2575) PATCH with invalid credentials returns no result
* [MXS-2574](https://jira.mariadb.org/browse/MXS-2574) maxctrl alter user doesn't work on current user
* [MXS-2569](https://jira.mariadb.org/browse/MXS-2569) No newline sent with large schemas
* [MXS-2563](https://jira.mariadb.org/browse/MXS-2563) Failing debug assertion at rwsplitsession.cc:1129 : m_expected_responses == 0
* [MXS-2562](https://jira.mariadb.org/browse/MXS-2562) Oracle's MySQL Connector/ODBC gets packets out-of-order errors with .NET
* [MXS-2560](https://jira.mariadb.org/browse/MXS-2560) KILL with recursive services doesn't work
* [MXS-2551](https://jira.mariadb.org/browse/MXS-2551) Monitors add deprecated parameters to persisted config files
* [MXS-2550](https://jira.mariadb.org/browse/MXS-2550) qlafilter escapes newline with two quotes and a space
* [MXS-2549](https://jira.mariadb.org/browse/MXS-2549) prelink can corrupt maxctrl
* [MXS-2547](https://jira.mariadb.org/browse/MXS-2547) Stop MaxScale during Rest-API query cause process hung
* [MXS-2521](https://jira.mariadb.org/browse/MXS-2521) COM_STMT_EXECUTE maybe return empty result
* [MXS-2490](https://jira.mariadb.org/browse/MXS-2490) Unknown prepared statement handler (0) given to mysqld_stmt_execute
* [MXS-2461](https://jira.mariadb.org/browse/MXS-2461) Unexpected internal state with ReadWriteSplit and proxy_protocol=true
* [MXS-2442](https://jira.mariadb.org/browse/MXS-2442) "Param1 reported as VARIABLE" warnings
* [MXS-2367](https://jira.mariadb.org/browse/MXS-2367) No warning about missing REPLICATION SLAVE

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
