# MariaDB MaxScale 2.1.7 Release Notes -- 2017-09-11

Release 2.1.7 is a GA release.

This document describes the changes in release 2.1.7, when compared to
release [2.1.6](MaxScale-2.1.6-Release-Notes.md).

If you are upgrading from release 2.0, please also read the following
release notes:
[2.1.6](./MaxScale-2.1.6-Release-Notes.md)
[2.1.5](./MaxScale-2.1.5-Release-Notes.md)
[2.1.4](./MaxScale-2.1.4-Release-Notes.md)
[2.1.3](./MaxScale-2.1.3-Release-Notes.md)
[2.1.2](./MaxScale-2.1.2-Release-Notes.md)
[2.1.1](./MaxScale-2.1.1-Release-Notes.md)
[2.1.0](./MaxScale-2.1.0-Release-Notes.md)

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Persistent connection statistics

The output of `show servers` now shows the number of times a connection was
taken from a server's pool as well as the ratio of connections taken from the
pool versus newly created connections.

### Logging

When known, the session id will be included in all logged messages. This allows
a range of logged messages related to a particular session (that is, client) to
be bound together, and makes it easier to investigate problems. In practice this
is visible so that if a logged message earlier looked like
```
2017-08-30 12:20:49   warning: [masking] The rule ...
```
it will now look like
```
2017-08-30 12:20:49   warning: (4711) [masking] The rule ...
```
where `4711` is the session id.

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.1.7.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.1.7)

* (MXS-1396)[https://jira.mariadb.org/browse/MXS-1396] Persistent connections hang with Percona Server 5.6.37-82.2-log
* (MXS-1395)[https://jira.mariadb.org/browse/MXS-1395] SELECT NAMES FROM TABLE is not parsed completely
* (MXS-1385)[https://jira.mariadb.org/browse/MXS-1385] Monitor script arguments can be truncated
* (MXS-1384)[https://jira.mariadb.org/browse/MXS-1384] maxscale.cnf script field length limitation
* (MXS-1380)[https://jira.mariadb.org/browse/MXS-1380] UNION is partially parsed
* (MXS-1379)[https://jira.mariadb.org/browse/MXS-1379] Undefined outcome on schemarouter query conflict
* (MXS-1375)[https://jira.mariadb.org/browse/MXS-1375] Reused connections get multiple replies to COM_CHANGE_USER
* (MXS-1374)[https://jira.mariadb.org/browse/MXS-1374] Persistent connections can't be altered at runtime
* (MXS-1366)[https://jira.mariadb.org/browse/MXS-1366] Abrupt disconnections with persistent connections
* (MXS-1365)[https://jira.mariadb.org/browse/MXS-1365] Write to invalid memory in avrorouter
* (MXS-1363)[https://jira.mariadb.org/browse/MXS-1363] Servers with zero weight aren't used by readconnroute
* (MXS-1341)[https://jira.mariadb.org/browse/MXS-1341] binlog checksums break avrorouter processing

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is maxscale-X.Y.Z.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
