# MariaDB MaxScale 2.5.13 Release Notes -- 2021-06-04

Release 2.5.13 is a GA release.

This document describes the changes in release 2.5.13, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3589](https://jira.mariadb.org/browse/MXS-3589) qc_sqlite handles current_timestamp etc. explicitly
* [MXS-3586](https://jira.mariadb.org/browse/MXS-3586) Maxscale tries to execute write statement on slave
* [MXS-3585](https://jira.mariadb.org/browse/MXS-3585) query classifier crashes after upgrade from 2.5.11 to 2.5.12
* [MXS-3582](https://jira.mariadb.org/browse/MXS-3582) [readwritesplit] Failed to execute session command
* [MXS-3581](https://jira.mariadb.org/browse/MXS-3581) Replicator component doesn't check node state for every operation

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
