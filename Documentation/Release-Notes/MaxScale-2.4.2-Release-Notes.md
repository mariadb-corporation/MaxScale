# MariaDB MaxScale 2.4.2 Release Notes

Release 2.4.2 is a GA release.

This document describes the changes in release 2.4.2, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2650](https://jira.mariadb.org/browse/MXS-2650) Connector-C-based connections do not use SSL even when configured
* [MXS-2631](https://jira.mariadb.org/browse/MXS-2631) Duplicate tables found, but it's system tables (information_schema.*, mysql.*)
* [MXS-2612](https://jira.mariadb.org/browse/MXS-2612) Use-after-free in cache filter

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
