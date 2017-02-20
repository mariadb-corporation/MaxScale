# MariaDB MaxScale 2.1.1 Release Notes

Release 2.1.1 is a Beta release.

This document describes the changes in release 2.1.1, when compared to
release 2.1.0.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## New Features

### Failover Recovery for MySQL Monitor

The `failover_recovery` option allows the failed nodes to rejoin the cluster
after a failover has been triggered. This makes it possible for external actors
to recover the failed nodes without having to manually clear the maintenance
mode.

For more information about the failover mode and how it works, read the
[MySQL Monitor](../Monitors/MySQL-Monitor.md) documentation.

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 2.1.0.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20resolution%20in%20(Fixed%2C%20Done)%20AND%20fixVersion%20%3D%202.1.1%20AND%20fixVersion%20NOT%20IN%20(2.1.0))

*

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is X.Y.Z. Further, *master* always refers to the latest released non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
