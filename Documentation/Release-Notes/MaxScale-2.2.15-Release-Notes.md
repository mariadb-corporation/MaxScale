# MariaDB MaxScale 2.2.15 Release Notes

Release 2.2.15 is a GA release.

This document describes the changes in release 2.2.15, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2066](https://jira.mariadb.org/browse/MXS-2066) Result buffering is not always disabled
* [MXS-2064](https://jira.mariadb.org/browse/MXS-2064) Loading of users with MariaDB 10.2.10 fails
* [MXS-2060](https://jira.mariadb.org/browse/MXS-2060) Users are loaded from servers in maintenance
* [MXS-2052](https://jira.mariadb.org/browse/MXS-2052) readwritesplit doesn't explain why session closes
* [MXS-2045](https://jira.mariadb.org/browse/MXS-2045) Avrorouter tutorial is not good enough
* [MXS-2043](https://jira.mariadb.org/browse/MXS-2043) Error  "The MariaDB server is running with the --read-only  option" for "select  for update"

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
