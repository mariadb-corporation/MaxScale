# MariaDB MaxScale 2.1.1 Release Notes -- 2017-03-14

Release 2.1.1 is a Beta release.

This document describes the changes in release 2.1.1, when compared to
release [2.1.0](MaxScale-2.1.0-Release-Notes.md).

If you are upgrading from release 2.0, please also read the following
release notes:
[2.1.0](./MaxScale-2.1.0-Release-Notes.md)

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Cache

* The cache will now _also_ be used and populated in a transaction that is
_not_ explicitly read only, but only until the first statement that modifies
the database is encountered.
* SELECT statements that refer to user or system variables are not cached.
* SELECT statements using functions whose result depend upon the current
user or context are not cached. Examples of such functions are `USER()`,
`RAND()` or `CURRENT_TIME()`.

### Firewall Filter

* Prepared statements are now treated exactly like non-prepared statements.
* Statements can now be accepted/rejected based upon function usage.

*NOTE* Both of these features were available already in _2.1.0_.

## Dropped Features

### MaxAdmin

The following deprecated commands have been removed:

* `enable log [debug|trace|message]`
* `disable log [debug|trace|message]`
* `enable sessionlog [debug|trace|message]`
* `disable sessionlog [debug|trace|message]`

The following commands have been deprecated:

* `enable sessionlog-priority <session-id> [debug|info|notice|warning]`
* `disable sessionlog-priority <session-id> [debug|info|notice|warning]`

The commands can be issued, but have no effect.

## New Features

### Failover Recovery for MySQL Monitor

The `failover_recovery` option allows the failed nodes to rejoin the cluster
after a failover has been triggered. This makes it possible for external actors
to recover the failed nodes without having to manually clear the maintenance
mode.

For more information about the failover mode and how it works, please read the
[MySQL Monitor](../Monitors/MySQL-Monitor.md) documentation.

### GSSAPI

_GASSAPI_ based authentication can now be used with MaxScale.

For more information, please read the
[GSSAPI Authentication](../Authenticators/GSSAPI-Authenticator.md) documentation.

NOTE This feature was available already in _2.1.0_.

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 2.1.0.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20resolution%20in%20(Fixed%2C%20Done)%20AND%20fixVersion%20%3D%202.1.1%20AND%20fixVersion%20NOT%20IN%20(2.1.0))

* [MXS-1178](https://jira.mariadb.org/browse/MXS-1178) master_accept_reads doesn't work with detect_replication_lag
* [MXS-1165](https://jira.mariadb.org/browse/MXS-1165) MaxInfo eat too much memory when getting list of session and client.
* [MXS-1143](https://jira.mariadb.org/browse/MXS-1143) Add support for new MariaDB 10.2 flags
* [MXS-1130](https://jira.mariadb.org/browse/MXS-1130) Unexpected length encoding 'ff' encountered
* [MXS-1081](https://jira.mariadb.org/browse/MXS-1081) Avro data file corruption
* [MXS-1077](https://jira.mariadb.org/browse/MXS-1077) Resource Leak
* [MXS-759](https://jira.mariadb.org/browse/MXS-759) Starting MaxScale from command line fails on CentOS 7

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
