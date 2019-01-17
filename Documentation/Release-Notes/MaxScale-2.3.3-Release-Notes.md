# MariaDB MaxScale 2.3.3 Release Notes

Release 2.3.3 is a GA release.

This document describes the changes in release 2.3.3, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2266](https://jira.mariadb.org/browse/MXS-2266) COM_STMT_CLOSE causes a warning to be logged
* [MXS-2259](https://jira.mariadb.org/browse/MXS-2259) Maxscale consumes large amounts of memory even with buffer limits set.
* [MXS-2258](https://jira.mariadb.org/browse/MXS-2258) GaleraMon crashes if it monitors a server that is not Galera-enabled
* [MXS-2248](https://jira.mariadb.org/browse/MXS-2248) INSERT sent to all nodes
* [MXS-2242](https://jira.mariadb.org/browse/MXS-2242) MaxScale does not recognize builtin read-only functions
* [MXS-2241](https://jira.mariadb.org/browse/MXS-2241) Conflicting parameters are not detected
* [MXS-2239](https://jira.mariadb.org/browse/MXS-2239) Crash in galeramon
* [MXS-2224](https://jira.mariadb.org/browse/MXS-2224) MaxScale 2.3.2 Memory leaks causes OOM
* [MXS-2217](https://jira.mariadb.org/browse/MXS-2217) maxscale crash with signal 11
* [MXS-2214](https://jira.mariadb.org/browse/MXS-2214) Documentation examples use whitespace in object names, which is invalid
* [MXS-2207](https://jira.mariadb.org/browse/MXS-2207) qc_mysqlembedded does not classify SET STATEMENT ... FOR UPDATE correctly.
* [MXS-2200](https://jira.mariadb.org/browse/MXS-2200) Setting a static variable via maxctrl gives a non accurate error message "Unknown global parameter"

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
