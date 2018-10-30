# MariaDB MaxScale 2.2.16 Release Notes

Release 2.2.16 is a GA release.

This document describes the changes in release 2.2.16, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2119](https://jira.mariadb.org/browse/MXS-2119) Files and directories are created with wrong permissions
* [MXS-2117](https://jira.mariadb.org/browse/MXS-2117) Old style queries aren't used when MDEV-13453 is encountered
* [MXS-2115](https://jira.mariadb.org/browse/MXS-2115) Automatic version detection doesn't work
* [MXS-2111](https://jira.mariadb.org/browse/MXS-2111) After SET PASSWORD done on the database, MaxScale not able to connect
* [MXS-2108](https://jira.mariadb.org/browse/MXS-2108) Session not immediately closed when all connections are lost
* [MXS-2103](https://jira.mariadb.org/browse/MXS-2103) ReadWriteSplit, SELECT on fully qualified temporary table is wrongly routed to Slave
* [MXS-2073](https://jira.mariadb.org/browse/MXS-2073) TCP_NODELAY not enabled for client socket
* [MXS-2049](https://jira.mariadb.org/browse/MXS-2049) Kerberos authentication not working or not clearly documented

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
