# MariaDB MaxScale 2.2.3 Release Notes -- 2018-03-09

Release 2.2.3 is a GA release.

This document describes the changes in release 2.2.3, when compared to
release 2.2.2.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### GTID output in MaxCtrl `list servers`

The output of the `list servers` command now has a GTID column. If a server is
being monitored by the mariadbmon monitor, the current GTID position will be
displayed in the newly added column. If no GTID is available, an empty value is
returned.

### MaxAdmin input from scripts

The failure to set terminal attributes for MaxScale is no longer considered an
error as scripts most often do not have an actual terminal that control the
process. This means that passwords and other commands can be passed to MaxAdmin
without a controlling terminal.

### MaxCtrl password input

MaxCtrl can now query the password from the user. This allows passwords to be
given without giving them as process arguments.

## Dropped Features

## New Features

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.2.3.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.2.3)

* [MXS-1698](https://jira.mariadb.org/browse/MXS-1698)  error:140940F5:SSL routines:ssl3_read_bytes:unexpected record
* [MXS-1697](https://jira.mariadb.org/browse/MXS-1697) MaxScale 2.2.2 missing avrorouter library
* [MXS-1693](https://jira.mariadb.org/browse/MXS-1693) In Maxscale 2.2.2 getting users with native password from mysql backends does not work
* [MXS-1688](https://jira.mariadb.org/browse/MXS-1688) Some date functions are not parsed properly with schemarouter
* [MXS-1684](https://jira.mariadb.org/browse/MXS-1684) Empty space on a line in rule file confuses dbfwfilter which refuses to start
* [MXS-1683](https://jira.mariadb.org/browse/MXS-1683) Commands that take passwords should allow input from stdin and not just from controlling terminals

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
