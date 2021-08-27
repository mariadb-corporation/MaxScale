# MariaDB MaxScale 2.5.15 Release Notes -- 2021-08-18

Release 2.5.15 is a GA release.

This document describes the changes in release 2.5.15, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3719](https://jira.mariadb.org/browse/MXS-3719) Readwritesplit logs wrong warning when replication lag is not available
* [MXS-3704](https://jira.mariadb.org/browse/MXS-3704) MaxScale always advertises the SESSION_TRACK capability even with servers that don't support it (XPand)
* [MXS-3703](https://jira.mariadb.org/browse/MXS-3703) Requirement to use cluster with xpandmon is not documented
* [MXS-3698](https://jira.mariadb.org/browse/MXS-3698) "maxctrl classify" fails with an exception
* [MXS-3695](https://jira.mariadb.org/browse/MXS-3695) Causal Consistency with MaxScale's Read/Write Split Router issue
* [MXS-3694](https://jira.mariadb.org/browse/MXS-3694) MXS - crash when cache is used with invalidate and hard_ttl
* [MXS-3692](https://jira.mariadb.org/browse/MXS-3692) Improve host pattern error message
* [MXS-3679](https://jira.mariadb.org/browse/MXS-3679) Mismatching user or source prevents session creation
* [MXS-3674](https://jira.mariadb.org/browse/MXS-3674) Deadlock in binlogrouter
* [MXS-3673](https://jira.mariadb.org/browse/MXS-3673) Draining servers is not documented
* [MXS-3550](https://jira.mariadb.org/browse/MXS-3550) write statistic is incremented for slaves
* [MXS-3532](https://jira.mariadb.org/browse/MXS-3532) Do not allow Galera master to be set to Drain
* [MXS-3508](https://jira.mariadb.org/browse/MXS-3508) causal_reads=global results in missing data reads
* [MXS-3478](https://jira.mariadb.org/browse/MXS-3478) admin_host missed on the static configuration parameters list
* [MXS-3474](https://jira.mariadb.org/browse/MXS-3474) Strange persistent pool connection stats
* [MXS-3299](https://jira.mariadb.org/browse/MXS-3299) Parse error when connecting (through binlog router) from mysql-connector-j

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
