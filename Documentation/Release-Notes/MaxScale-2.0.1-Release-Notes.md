# MariaDB MaxScale 2.0.1 Release Notes

Release 2.0.1 is a GA release.

This document describes the changes in release 2.0.1, when compared to
[release 2.0.0](MaxScale-2.0.0-Release-Notes.md).

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## License

The license of MaxScale has been changed from GPLv2 to MariaDB BSL.

For more information about MariaDB BSL, please refer to
[MariaDB BSL](https://www.mariadb.com/bsl).

## Updated Features

### Routing hint priority change

Routing hints now have the highest priority when a routing decision is made. If
there is a conflict between the original routing decision made by the
readwritesplit and the routing hint attached to the query, the routing hint
takes higher priority.

What this change means is that, if a query would normally be routed to the
master but the routing hint instructs the router to route it to the slave, it
would be routed to the slave.

**WARNING**: This change can alter the way some statements are routed and could
  possibly cause data loss, corruption or inconsisteny. Please consult the [Hint
  Syntax](../Reference/Hint-Syntax.md) and
  [ReadWriteSplit](../Routers/ReadWriteSplit.md) documentation before using
  routing hints.

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 2.0.1.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20in%20(2.0.0%2C%202.0.1)%20AND%20resolved%20%3E%3D%20-21d%20ORDER%20BY%20priority%20DESC%2C%20updated%20DESC)

* [MXS-847](https://jira.mariadb.org/browse/MXS-847): server_down event is executed 8 times due to putting sever into maintenance mode
* [MXS-845](https://jira.mariadb.org/browse/MXS-845): "Server down" event is re-triggered after maintenance mode is repeated
* [MXS-842](https://jira.mariadb.org/browse/MXS-842): Unexpected / undocumented behaviour when multiple available masters from mmmon monitor
* [MXS-846](https://jira.mariadb.org/browse/MXS-846): MMMon: Maintenance mode on slave logs error message every second


## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md)
document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is maxscale-X.Y.Z. Further, *master* always refers to the latest released
non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
