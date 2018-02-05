# MariaDB MaxScale 2.2.2 Release Notes

Release 2.2.2 is a XXX release.

This document describes the changes in release 2.2.2, when compared to
release 2.2.1.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

## Dropped Features

## New Features

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.2.2.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.2.2)

* [MXS-1630](https://jira.mariadb.org/browse/MXS-1630) MaxCtrl binary are not included by default in MaxScale package
* [MXS-1620](https://jira.mariadb.org/browse/MXS-1620) CentOS package symbols are stripped
* [MXS-1615](https://jira.mariadb.org/browse/MXS-1615) Masking filter accesses wrong command argument.
* [MXS-1614](https://jira.mariadb.org/browse/MXS-1614) MariaDBMon yet adding mysqlbackend as the protocol instead of mariadbbackend
* [MXS-1606](https://jira.mariadb.org/browse/MXS-1606) After enabling detect_replication_lag MariaDBMon does not create the maxscale_schema.replication_heartbeat database and table
* [MXS-1604](https://jira.mariadb.org/browse/MXS-1604) PamAuth Default Authentication is Different from MariaDB
* [MXS-1591](https://jira.mariadb.org/browse/MXS-1591) Adding get_lock and release_lock support
* [MXS-1539](https://jira.mariadb.org/browse/MXS-1539) Authentication data should be thread specific

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
