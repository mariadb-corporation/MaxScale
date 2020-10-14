# MariaDB MaxScale 2.4.13 Release Notes

Release 2.4.13 is a GA release.

This document describes the changes in release 2.4.13, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3220](https://jira.mariadb.org/browse/MXS-3220) MaxScale crashes in gwbuf_set_type() upon query retry
* [MXS-3200](https://jira.mariadb.org/browse/MXS-3200) Abort due to double free or corruption after "Write to Client DCB ... state DCB_STATE_POLLING failed"
* [MXS-3198](https://jira.mariadb.org/browse/MXS-3198) MariadbMon documentation needs to take 10.5 privilege changes into account
* [MXS-3177](https://jira.mariadb.org/browse/MXS-3177) Fix download link in documentation
* [MXS-3165](https://jira.mariadb.org/browse/MXS-3165) cache_inside_transactions vs cache_in_transactions
* [MXS-3149](https://jira.mariadb.org/browse/MXS-3149) Monitor should remove [Master] when starting swithover
* [MXS-3143](https://jira.mariadb.org/browse/MXS-3143) FOUND_ROWS() not routed to previous target
* [MXS-3132](https://jira.mariadb.org/browse/MXS-3132) Monitor timeout defaults are wrong
* [MXS-3131](https://jira.mariadb.org/browse/MXS-3131) Monitor module not displayed in `show monitors`
* [MXS-3123](https://jira.mariadb.org/browse/MXS-3123) Documentation: KB page for 2.4 does not mention maxctrl way to rotate the log
* [MXS-2680](https://jira.mariadb.org/browse/MXS-2680) Monitor type not shown in show monitors 

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
