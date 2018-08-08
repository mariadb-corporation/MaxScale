# MariaDB MaxScale 2.2.13 Release Notes -- 2018-08-08

Release 2.2.13 is a GA release.

This document describes the changes in release 2.2.13, when compared to
release 2.2.12.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-1997](https://jira.mariadb.org/browse/MXS-1997) Object names are limited to 49 characters
* [MXS-1983](https://jira.mariadb.org/browse/MXS-1983) Failed to add dcb to epoll set
* [MXS-1961](https://jira.mariadb.org/browse/MXS-1961) Standalone master loses master status

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
