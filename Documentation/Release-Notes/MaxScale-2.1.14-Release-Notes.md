# MariaDB MaxScale 2.1.14 Release Notes

Release 2.1.14 is a GA release.

This document describes the changes in release 2.1.14, when compared
to release [2.1.13](MaxScale-2.1.13-Release-Notes.md).

If you are upgrading from release 2.0, please also read the following
release notes:

* [2.1.13](./MaxScale-2.1.13-Release-Notes.md)
* [2.1.12](./MaxScale-2.1.12-Release-Notes.md)
* [2.1.11](./MaxScale-2.1.11-Release-Notes.md)
* [2.1.10](./MaxScale-2.1.10-Release-Notes.md)
* [2.1.9](./MaxScale-2.1.9-Release-Notes.md)
* [2.1.8](./MaxScale-2.1.8-Release-Notes.md)
* [2.1.7](./MaxScale-2.1.7-Release-Notes.md)
* [2.1.6](./MaxScale-2.1.6-Release-Notes.md)
* [2.1.5](./MaxScale-2.1.5-Release-Notes.md)
* [2.1.4](./MaxScale-2.1.4-Release-Notes.md)
* [2.1.3](./MaxScale-2.1.3-Release-Notes.md)
* [2.1.2](./MaxScale-2.1.2-Release-Notes.md)
* [2.1.1](./MaxScale-2.1.1-Release-Notes.md)
* [2.1.0](./MaxScale-2.1.0-Release-Notes.md)

For any problems you encounter, please consider submitting a bug report at
[Jira](https://jira.mariadb.org).

## New Features

### Users Refresh Time

It is now possible to adjust how frequently MaxScale may refresh
the users of service. Please refer to the documentation for
[details](../Getting-Started/Configuration-Guide.md#users_refresh_time).

### Local Address

It is now possible to specify what local address MaxScale should
use when connecting to servers. Please refer to the documentation
for [details](../Getting-Started/Configuration-Guide.md#local_address).

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.1.14.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.1.14)

* [MXS-1627](https://jira.mariadb.org/browse/MXS-1627) MySQLAuth loads users that use authentication plugins
* [MXS-1620](https://jira.mariadb.org/browse/MXS-1620) CentOS package symbols are stripped
* [MXS-1602](https://jira.mariadb.org/browse/MXS-1602) cannot connect to maxinfo with python client
* [MXS-1601](https://jira.mariadb.org/browse/MXS-1601) maxinfo crash at execute query 'flush;'
* [MXS-1600](https://jira.mariadb.org/browse/MXS-1600) maxscale it seen to not coop well with lower-case-table-names=1 on cnf
* [MXS-1576](https://jira.mariadb.org/browse/MXS-1576) Maxscale crashes when starting if .avro and .avsc files are present
* [MXS-1543](https://jira.mariadb.org/browse/MXS-1543) Avrorouter doesn't detect MIXED or STATEMENT format replication
* [MXS-1416](https://jira.mariadb.org/browse/MXS-1416) maxscale should not try to do anything when started with --config-check

## Packaging

RPM and Debian packages are provided for the Linux distributions supported by
MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is maxscale-X.Y.Z.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
