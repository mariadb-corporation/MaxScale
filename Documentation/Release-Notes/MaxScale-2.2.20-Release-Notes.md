# MariaDB MaxScale 2.2.20 Release Notes -- 2019-03-15

Release 2.2.20 is a GA release.

This document describes the changes in release 2.2.20, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2375](https://jira.mariadb.org/browse/MXS-2375) maxctrl stop maxscale is confusing
* [MXS-2368](https://jira.mariadb.org/browse/MXS-2368) maxctrl requires password on command line and cannot change user password
* [MXS-2335](https://jira.mariadb.org/browse/MXS-2335) lower_case_table_names doesn't work
* [MXS-2322](https://jira.mariadb.org/browse/MXS-2322) Null characters in passwords break maxctrl
* [MXS-2311](https://jira.mariadb.org/browse/MXS-2311) connection_keepalive and error : (4852) Unexpected internal state: received response 0x00 from server
* [MXS-2296](https://jira.mariadb.org/browse/MXS-2296) maxscale-system-test does not generate core dumps
* [MXS-2268](https://jira.mariadb.org/browse/MXS-2268) readwritesplitter is not routing queries properly in Maxscale 2.3.2
* [MXS-2206](https://jira.mariadb.org/browse/MXS-2206) Cmake version check fails with version = 3.10.2

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
