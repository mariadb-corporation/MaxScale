# MariaDB MaxScale 2.3.18 Release Notes

Release 2.3.18 is a GA release.

This document describes the changes in release 2.3.18, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2917](https://jira.mariadb.org/browse/MXS-2917) qc_sqlite leaks memory with complex CREATE TABLE query
* [MXS-2899](https://jira.mariadb.org/browse/MXS-2899) Server charset set to latin1 on retrieval query failure
* [MXS-2896](https://jira.mariadb.org/browse/MXS-2896) Server wrongly in Running state after failure to connect
* [MXS-2894](https://jira.mariadb.org/browse/MXS-2894) Fails to download avro tar file
* [MXS-2883](https://jira.mariadb.org/browse/MXS-2883) session closed by maxscale when it received "auth switch request" packet from backend server
* [MXS-2832](https://jira.mariadb.org/browse/MXS-2832) Failover timing estimates are not documented
* [MXS-2810](https://jira.mariadb.org/browse/MXS-2810) maxscale process still running after uninstalling maxscale package
* [MXS-2277](https://jira.mariadb.org/browse/MXS-2277) Calling MaxAdmin/MaxCtrl in a monitor event script can cause a deadlock

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
