# MariaDB MaxScale 2.3.12 Release Notes -- 2019-09-19

Release 2.3.12 is a GA release.

This document describes the changes in release 2.3.12, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2684](https://jira.mariadb.org/browse/MXS-2684) Backend DCB taken from persistent pool is not subject to throttling
* [MXS-2674](https://jira.mariadb.org/browse/MXS-2674) QC bugs
* [MXS-2652](https://jira.mariadb.org/browse/MXS-2652) Maintenance mode lost if node is shutdown
* [MXS-2642](https://jira.mariadb.org/browse/MXS-2642) PAMAuth does not eliminate duplicate PAM services during authentication
* [MXS-2633](https://jira.mariadb.org/browse/MXS-2633) Pam authentication doesn't work with server 10.4
* [MXS-2631](https://jira.mariadb.org/browse/MXS-2631) Duplicate tables found, but it's system tables (information_schema.*, mysql.*)
* [MXS-2609](https://jira.mariadb.org/browse/MXS-2609) Maxscale crash in RWSplitSession::retry_master_query()
* [MXS-2576](https://jira.mariadb.org/browse/MXS-2576) Columnstore Monitor inaccurately labels a UM as slave
* [MXS-2198](https://jira.mariadb.org/browse/MXS-2198) Lacking tarball installation steps in documents

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
