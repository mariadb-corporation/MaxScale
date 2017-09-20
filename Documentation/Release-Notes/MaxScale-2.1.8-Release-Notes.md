# MariaDB MaxScale 2.1.8 Release Notes -- 2017-09-20

Release 2.1.8 is a GA release.

This document describes the changes in release 2.1.8, when compared to
release [2.1.7](MaxScale-2.1.7-Release-Notes.md).

If you are upgrading from release 2.0, please also read the following
release notes:
[2.1.7](./MaxScale-2.1.7-Release-Notes.md)
[2.1.6](./MaxScale-2.1.6-Release-Notes.md)
[2.1.5](./MaxScale-2.1.5-Release-Notes.md)
[2.1.4](./MaxScale-2.1.4-Release-Notes.md)
[2.1.3](./MaxScale-2.1.3-Release-Notes.md)
[2.1.2](./MaxScale-2.1.2-Release-Notes.md)
[2.1.1](./MaxScale-2.1.1-Release-Notes.md)
[2.1.0](./MaxScale-2.1.0-Release-Notes.md)

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.1.8.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.1.8)

* (MXS-1421)[https://jira.mariadb.org/browse/MXS-1421] Even though limit is reached, maxrows continues to buffer resultset.
* (MXS-1418)[https://jira.mariadb.org/browse/MXS-1418] remove server does not drain node
* (MXS-1414)[https://jira.mariadb.org/browse/MXS-1414] About  Presistent Connection  Mysql Gone away
* (MXS-1412)[https://jira.mariadb.org/browse/MXS-1412] Performance issue with MaxRows filter
* (MXS-1411)[https://jira.mariadb.org/browse/MXS-1411] error : (46) [maxrows] Received data from the backend although we were expecting nothing.
* (MXS-1409)[https://jira.mariadb.org/browse/MXS-1409] maxadmin socket with port results in help
* (MXS-1400)[https://jira.mariadb.org/browse/MXS-1400] Crash with OpenSSL 1.1
* (MXS-1396)[https://jira.mariadb.org/browse/MXS-1396] Persistent connections hang with Percona Server 5.6.37-82.2-log

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is maxscale-X.Y.Z.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
