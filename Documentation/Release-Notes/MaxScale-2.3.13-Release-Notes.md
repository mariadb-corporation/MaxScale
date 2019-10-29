# MariaDB MaxScale 2.3.13 Release Notes

Release 2.3.13 is a GA release.

This document describes the changes in release 2.3.13, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2733](https://jira.mariadb.org/browse/MXS-2733) UTC_TIMESTAMP not recognized as a built-in function
* [MXS-2721](https://jira.mariadb.org/browse/MXS-2721) MaxScale 2.4.2 crashes on different lxc containers with similar error
* [MXS-2720](https://jira.mariadb.org/browse/MXS-2720) maxctrl and maxadmin report negative number of connections to a service
* [MXS-2713](https://jira.mariadb.org/browse/MXS-2713) SET PASSWORD statement sent to all nodes, not only to master
* [MXS-2712](https://jira.mariadb.org/browse/MXS-2712) maxctrl help does not explain meaning of the output columns of some commands
* [MXS-2706](https://jira.mariadb.org/browse/MXS-2706) Maxscale maxinfo plugin breaks maxscale_exporter (JSON output all strings)
* [MXS-2699](https://jira.mariadb.org/browse/MXS-2699) Two QC bugs
* [MXS-2694](https://jira.mariadb.org/browse/MXS-2694) COM_BINLOG_DUMP confuses readwritesplit
* [MXS-2688](https://jira.mariadb.org/browse/MXS-2688) SET should be classified as QUERY_TYPE_SESSION_WRITE
* [MXS-2673](https://jira.mariadb.org/browse/MXS-2673) Sys var and user var combined issue
* [MXS-2645](https://jira.mariadb.org/browse/MXS-2645) Session client count is not always decremented when closing session
* [MXS-2639](https://jira.mariadb.org/browse/MXS-2639) maxinfo memory leak
* [MXS-2620](https://jira.mariadb.org/browse/MXS-2620) Document that shutting down the master can break auto_rejoin and lose transactions
* [MXS-2610](https://jira.mariadb.org/browse/MXS-2610) [AVRO ROUTER] Maxscale Crash
* [MXS-2600](https://jira.mariadb.org/browse/MXS-2600) Documentation is inconsistent regarding privileges for MariaDB Monitor

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
