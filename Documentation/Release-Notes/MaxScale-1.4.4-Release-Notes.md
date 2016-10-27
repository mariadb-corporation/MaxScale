
# MariaDB MaxScale 1.4.4 Release Notes

Release 1.4.4 is a GA release.

This document describes the changes in release 1.4.4, when compared to
release 1.4.3.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## New Features
Binlog Server improvements:
 - MXS-584:
 
   SET autocommit = 0|1
   and
   SET @@session.autocommit = on|off
   are handled.

- SELECT USER()

  this is a new command that returns 'user@host'
- Charset:

  In previous versions only 'latin' and 'utf8' were supported during registration phase.
  Connecting clients can now request any SET NAMES XXX  option without receiving an error message from MaxScale

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 1.4.3.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20resolution%20in%20(Fixed%2C%20Done)%20AND%20fixVersion%20%3D%201.4.4)

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
