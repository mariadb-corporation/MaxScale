# MariaDB MaxScale 2.2.8 Release Notes -- 2018-06

Release 2.2.8 is a GA release.

This document describes the changes in release 2.2.8, when compared to
release 2.2.7.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Bug fixes

* [MXS-1889](https://jira.mariadb.org/browse/MXS-1889) A single remaining master is valid for readconnroute configured with 'router_options=slave'
* [MXS-1740](https://jira.mariadb.org/browse/MXS-1740) Hintfilter leaks memory

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/downloads/mariadb-tx/maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is X.Y.Z. Further, *master* always refers to the latest released non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
