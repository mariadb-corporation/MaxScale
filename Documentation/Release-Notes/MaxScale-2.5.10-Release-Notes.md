# MariaDB MaxScale 2.5.10 Release Notes

Release 2.5.10 is a GA release.

This document describes the changes in release 2.5.10, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3450](https://jira.mariadb.org/browse/MXS-3450) COM_FIELD_LIST is classified as a write
* [MXS-3449](https://jira.mariadb.org/browse/MXS-3449) Maxscale sets MASTER_GTID_WAIT timeout to zero, when causal_reads_timeout is less than 1s
* [MXS-3448](https://jira.mariadb.org/browse/MXS-3448) Unable to disable maxlog
* [MXS-3445](https://jira.mariadb.org/browse/MXS-3445) SET DEFAULT ROLE is not classified as a write
* [MXS-3436](https://jira.mariadb.org/browse/MXS-3436) Packet received out-of-order. Expected 1; got 3
* [MXS-3433](https://jira.mariadb.org/browse/MXS-3433) Monitor does not set session autocommit if it connects to a server first
* [MXS-3427](https://jira.mariadb.org/browse/MXS-3427) The 'INFORMATION_SCHEMA.SESSION_STATUS' feature is disabled; see the documentation for 'show_compatibility_56'

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
