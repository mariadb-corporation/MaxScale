# MariaDB MaxScale 2.0.2 Release Notes

Release 2.0.2 is a GA release.

This document describes the changes in release 2.0.2, when compared to
release [2.0.1](MaxScale-2.0.1-Release-Notes.md).

If you are upgrading from release 1.4.4, please also read the release
notes of release [2.0.0](./MaxScale-2.0.0-Release-Notes.md) and
release [2.0.1](./MaxScale-2.0.1-Release-Notes.md).

For any problems you encounter, please submit a bug report at
[Jira](https://jira.mariadb.org).

## Updated Features

### [MXS-978] (https://jira.mariadb.org/browse/MXS-978) Support for stale master in case of restart

In case where replication monitor gets a stale master status (slaves are down)
and maxscale gets restarted, master loses the stale master status and no writes
can happen.

To cater for this situation there is now a `set server <name> stale` command.

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 2.0.1.](https://jira.mariadb.org/browse/MXS-976?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.0.2)

* [MXS-1018](https://jira.mariadb.org/browse/MXS-1018): Internal connections don't use TLS
* [MXS-976](https://jira.mariadb.org/browse/MXS-976): Crash in libqc_sqlite
* [MXS-975](https://jira.mariadb.org/browse/MXS-975): TCP backlog is capped at 1280
* [MXS-970](https://jira.mariadb.org/browse/MXS-970): A fatal problem with maxscale automatically shut down
* [MXS-969](https://jira.mariadb.org/browse/MXS-969): use_sql_variables_in=master can break functionality of important session variables
* [MXS-967](https://jira.mariadb.org/browse/MXS-967): setting connection_timeout=value cause error : Not a boolean value
* [MXS-965](https://jira.mariadb.org/browse/MXS-965): galeramon erlaubt keine TLS verschlüsselte Verbindung
* [MXS-960](https://jira.mariadb.org/browse/MXS-960): MaxScale Binlog Server does not allow comma to be in password
* [MXS-957](https://jira.mariadb.org/browse/MXS-957): Temporary table creation from another temporary table isn't detected
* [MXS-955](https://jira.mariadb.org/browse/MXS-955): MaxScale 2.0.1 doesn't recognize user and passwd options in .maxadmin file
* [MXS-953](https://jira.mariadb.org/browse/MXS-953): Charset error when server configued in utf8mb4
* [MXS-942](https://jira.mariadb.org/browse/MXS-942): describe table query not routed to shard that contains the schema
* [MXS-917](https://jira.mariadb.org/browse/MXS-917): False error message about master not being in use

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

