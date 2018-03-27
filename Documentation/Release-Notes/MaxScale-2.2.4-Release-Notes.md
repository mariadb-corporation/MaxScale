# MariaDB MaxScale 2.2.4 Release Notes -- 2018-03

Release 2.2.4 is a GA release.

This document describes the changes in release 2.2.4, when compared to
release 2.2.3.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Masking is stricter

If a masking rule specifies the table/database in addition to the column
name, then if a resultset does not contain table/database information, it
is considered a match if the column name matches. Please consult the
[documentation](../Filters/Masking.md) for details.

## Dropped Features

## New Features

New configuration parameters `retain_last_statements` and
`dump_last_statements` that can be of help when debugging problems. Please
see the [configuration guide](../Getting-Started/Configuration-Guide.md)
for details.

## Bug fixes

* [MXS-1738](https://jira.mariadb.org/browse/MXS-1738) MaxScale crashes in debug mode if authentication fails.
* [MXS-1733](https://jira.mariadb.org/browse/MXS-1733) Data masking does not work with UNION queries
* [MXS-1731](https://jira.mariadb.org/browse/MXS-1731) Empty version_string is not detected
* [MXS-1730](https://jira.mariadb.org/browse/MXS-1730) Column alias named engine without backticks returns an error
* [MXS-1729](https://jira.mariadb.org/browse/MXS-1729) Luafilter ignores return value of global routeQuery function
* [MXS-1722](https://jira.mariadb.org/browse/MXS-1722) Switchover leads to error: Demotion failed due to an error in updating gtid:s.
* [MXS-1721](https://jira.mariadb.org/browse/MXS-1721) If two services share one filter there will a crash at exit.
* [MXS-1719](https://jira.mariadb.org/browse/MXS-1719) masking filter with readwritesplit router  problems
* [MXS-1717](https://jira.mariadb.org/browse/MXS-1717) When having two listeners use the same service show dbusers serviceName shows the user list twice
* [MXS-1716](https://jira.mariadb.org/browse/MXS-1716) show dbusers ...service... returns empty list when using PAMAuth
* [MXS-1714](https://jira.mariadb.org/browse/MXS-1714) local_address is not used by internal connections
* [MXS-1713](https://jira.mariadb.org/browse/MXS-1713) SchemaRouter unable to process SHOW DATABASES for a lot of schemas
* [MXS-1705](https://jira.mariadb.org/browse/MXS-1705) Maxscale 2.2.2 crashes on startup with CentOS 7
* [MXS-1701](https://jira.mariadb.org/browse/MXS-1701) Source building instructions are not correct
* [MXS-1689](https://jira.mariadb.org/browse/MXS-1689) Error message in case both port and socket are defined is not clear
* [MXS-1679](https://jira.mariadb.org/browse/MXS-1679) Maxscale does not detect failover executed by another Maxscale in 2 Maxscales + keepalived configuration

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
