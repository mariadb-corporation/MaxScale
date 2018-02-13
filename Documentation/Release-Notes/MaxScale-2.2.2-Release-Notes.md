# MariaDB MaxScale 2.2.2 Release Notes

Release 2.2.2 is a GA release.

This document describes the changes in release 2.2.2, when compared to
release 2.2.1.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### MaxCtrl Moved to `maxscale` Package

The MaxCtrl client is now a part of the main MaxScale package, `maxscale`. This
means that the `maxctrl` executable is now immediately available upon the
installation of MaxScale.

In the 2.2.1 beta version MaxCtrl was in its own package. If you have a previous
installation of MaxCtrl, please remove it before upgrading to MaxScale 2.2.2.

### MaxScale C++ CDC Connector Integration

The MaxScale C++ CDC Connector is now distributed as a part of MaxScale. The
connector libraries are in a separate package, `maxscale-cdc-connector`. Refer
to the [CDC Connector documentation](../Connectors/CDC-Connector.md) for more details.

## Dropped Features

## New Features

### Users Refresh Time

It is now possible to adjust how frequently MaxScale may refresh
the users of service. Please refer to the documentation for
[details](../Getting-Started/Configuration-Guide.md#users_refresh_time).

### Local Address

It is now possible to specify what local address MaxScale should
use when connecting to servers. Please refer to the documentation
for [details](../Getting-Started/Configuration-Guide.md#local_address).

### External master support for failover/switchover

Failover/switchover now tries to preserve replication from an external master
server. Check
[MariaDB Monitor documentation](../Monitors/MariaDB-Monitor.md#external-master-support)
for more information.

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.2.2.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.2.2)

* [MXS-1661](https://jira.mariadb.org/browse/MXS-1661) Excessive logging by MySQLAuth at authentication error (was: MySQLAuth SQLite database can be permanently locked)
* [MXS-1660](https://jira.mariadb.org/browse/MXS-1660) Failure to resolve hostname is considered an error
* [MXS-1654](https://jira.mariadb.org/browse/MXS-1654) MaxScale crashes if env-variables are used without substitute_variables=1 having been defined
* [MXS-1653](https://jira.mariadb.org/browse/MXS-1653) sysbench failed to initialize w/ MaxScale read/write splitter
* [MXS-1647](https://jira.mariadb.org/browse/MXS-1647) Module API version is not checked
* [MXS-1643](https://jira.mariadb.org/browse/MXS-1643) Too many monitor events are triggered
* [MXS-1641](https://jira.mariadb.org/browse/MXS-1641) Fix overflow in master id
* [MXS-1633](https://jira.mariadb.org/browse/MXS-1633) Need remove mutex in sqlite
* [MXS-1630](https://jira.mariadb.org/browse/MXS-1630) MaxCtrl binary are not included by default in MaxScale package
* [MXS-1628](https://jira.mariadb.org/browse/MXS-1628) Security scanner says MaxScale is vulnerable to ancient MySQL vulnerability
* [MXS-1620](https://jira.mariadb.org/browse/MXS-1620) CentOS package symbols are stripped
* [MXS-1615](https://jira.mariadb.org/browse/MXS-1615) Masking filter accesses wrong command argument.
* [MXS-1614](https://jira.mariadb.org/browse/MXS-1614) MariaDBMon yet adding mysqlbackend as the protocol instead of mariadbbackend
* [MXS-1606](https://jira.mariadb.org/browse/MXS-1606) After enabling detect_replication_lag MariaDBMon does not create the maxscale_schema.replication_heartbeat database and table
* [MXS-1604](https://jira.mariadb.org/browse/MXS-1604) PamAuth Default Authentication is Different from MariaDB
* [MXS-1591](https://jira.mariadb.org/browse/MXS-1591) Adding get_lock and release_lock support
* [MXS-1586](https://jira.mariadb.org/browse/MXS-1586) Mysqlmon switchover does not immediately detect bad new master
* [MXS-1583](https://jira.mariadb.org/browse/MXS-1583) Database firewall filter failing with multiple users statements in rules file
* [MXS-1539](https://jira.mariadb.org/browse/MXS-1539) Authentication data should be thread specific
* [MXS-1508](https://jira.mariadb.org/browse/MXS-1508) Failover is sometimes triggered on non-simple topologies

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
