# MariaDB MaxScale 21.06.16 Release Notes

**NOTE** MaxScale 6.4 was renamed to 21.06 in May 2024. Thus, what would have
been released as 6.4.16 in June, was released as 21.06.16. The purpose of this
change is to make the versioning scheme used by all MaxScale series
identical. 21.06 denotes the year and month when the first 6 release was made.

Release 21.06.16 is a GA release.

This document describes the changes in release 21.06.16, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-21.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-5067](https://jira.mariadb.org/browse/MXS-5067) Add "enforce_read_only_servers" feature to MariaDB Monitor

## Bug fixes

* [MXS-5095](https://jira.mariadb.org/browse/MXS-5095) Master Stickiness state is not documented
* [MXS-5090](https://jira.mariadb.org/browse/MXS-5090) ability to setup .secrets file location
* [MXS-5085](https://jira.mariadb.org/browse/MXS-5085) max_slave_connections=0 may create slave connections after a switchover
* [MXS-5083](https://jira.mariadb.org/browse/MXS-5083) ssl_version in MaxScale and tls_version in MariaDB behave differently
* [MXS-5082](https://jira.mariadb.org/browse/MXS-5082) Password encryption format change in 2.5 is not documented very well
* [MXS-5081](https://jira.mariadb.org/browse/MXS-5081) The values of ssl_version in MaxScale and tls_version in MariaDB accept different values
* [MXS-5074](https://jira.mariadb.org/browse/MXS-5074) Warning about missing slashes around regular expressions is confusing
* [MXS-5048](https://jira.mariadb.org/browse/MXS-5048) Problem in hostname matching when using regex (%) for user authentication
* [MXS-5039](https://jira.mariadb.org/browse/MXS-5039) cooperative_monitoring_locks can leave stale locks on a server if network breaks
* [MXS-5038](https://jira.mariadb.org/browse/MXS-5038) Maxscale key limitations
* [MXS-5031](https://jira.mariadb.org/browse/MXS-5031) enforce_read_only_slaves can set master to read_only
* [MXS-5021](https://jira.mariadb.org/browse/MXS-5021) gdb-stacktrace is incorrectly presented as a debug option
* [MXS-5014](https://jira.mariadb.org/browse/MXS-5014) During Failover Passive MaxScale route writes to the Old Master
* [MXS-5010](https://jira.mariadb.org/browse/MXS-5010) Session commands that are executed early are not validated
* [MXS-5009](https://jira.mariadb.org/browse/MXS-5009) --basedir is broken
* [MXS-4902](https://jira.mariadb.org/browse/MXS-4902) MariaDB Monitor command reset-replication can be started on a secondary MaxScale
* [MXS-4834](https://jira.mariadb.org/browse/MXS-4834) MaxScale should log a warning if failover may lose transactions

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the supported Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/#mariadb_platform-mariadb_maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
