# MariaDB MaxScale 2.4.14 Release Notes -- 2020-11-25

Release 2.4.14 is a GA release.

This document describes the changes in release 2.4.14, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

**NOTE** 2.4.14 is the last release that is made available for RHEL6, which reaches its EOL at the end of November.

## Bug fixes

* [MXS-3297](https://jira.mariadb.org/browse/MXS-3297) Extended MariaDB capabilities are not read correctly
* [MXS-3295](https://jira.mariadb.org/browse/MXS-3295) Layout of classify REST API endpoint stores non-parameter data in parameters object
* [MXS-3293](https://jira.mariadb.org/browse/MXS-3293) Backticks not stripped in USE statements.
* [MXS-3273](https://jira.mariadb.org/browse/MXS-3273) Connection lost when unrelated server loses Slave status
* [MXS-3272](https://jira.mariadb.org/browse/MXS-3272) maxctrl not prompt directy for the password
* [MXS-3240](https://jira.mariadb.org/browse/MXS-3240) Uom variable from maxscale api /maxscale/threads

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
