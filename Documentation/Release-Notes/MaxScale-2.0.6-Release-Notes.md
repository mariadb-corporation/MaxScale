# MariaDB MaxScale 2.0.6 Release Notes

Release 2.0.6 is a GA release.

This document describes the changes in release 2.0.6, when compared to
release [2.0.5](MaxScale-2.0.5-Release-Notes.md).

If you are upgrading from release 1.4, please also read the following
release notes:
[2.0.5](./MaxScale-2.0.5-Release-Notes.md),
[2.0.4](./MaxScale-2.0.4-Release-Notes.md),
[2.0.3](./MaxScale-2.0.3-Release-Notes.md),
[2.0.2](./MaxScale-2.0.2-Release-Notes.md),
[2.0.1](./MaxScale-2.0.1-Release-Notes.md) and
[2.0.0](./MaxScale-2.0.0-Release-Notes.md).

For any problems you encounter, please submit a bug report at
[Jira](https://jira.mariadb.org).

## Bug fixes

[Here](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.0.6)
is a list of bugs fixed since the release of MaxScale 2.0.5.

* [MXS-1244](https://jira.mariadb.org/browse/MXS-1244): MySQL monitor "detect_replication_lag=true" doesn't work with "mysql51_replication=true"
* [MXS-1221](https://jira.mariadb.org/browse/MXS-1221): Nagios plugin scripts does not process -S option properly
* [MXS-1218](https://jira.mariadb.org/browse/MXS-1218): Use correct conversion specifier when printing 64-bit integers
* [MXS-1216](https://jira.mariadb.org/browse/MXS-1216): Fatal error while converting data
* [MXS-1191](https://jira.mariadb.org/browse/MXS-1191): Alter statement to a table with no create statement.
* [MXS-1180](https://jira.mariadb.org/browse/MXS-1180): cdc.py not producing anything with JSON format, but does with AVRO
* [MXS-1178](https://jira.mariadb.org/browse/MXS-1178): master_accept_reads doesn't work with detect_replication_lag

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is derived
from the version of MaxScale. For instance, the tag of version `X.Y.Z` of MaxScale
is `maxscale-X.Y.Z`.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
