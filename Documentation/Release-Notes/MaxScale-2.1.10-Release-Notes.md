# MariaDB MaxScale 2.1.10 Release Notes

Release 2.1.10 is a GA release.

This document describes the changes in release 2.1.10, when compared
to release [2.1.9](MaxScale-2.1.9-Release-Notes.md).

If you are upgrading from release 2.0, please also read the following
release notes:

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

## Changed Features

### Internal Query Retries

The internal SQL queries that MaxScale executes to load database users as well
as monitor the database itself can now be automatically retried if they are
interrupted. The new global parameter, `query_retries` controls the number of
retry attempts each query will receive if it fails due to a network problem.
The `query_retry_timeout` global parameter controls the total timeout for the
retries.

To enable this functionality, add `query_retries=<number-of-retries>` under the
`[maxscale]` section where _<number-of-retries>_ is a positive integer.

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.1.10.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.1.10)

* [MXS-1468](https://jira.mariadb.org/browse/MXS-1468) Using dynamic commands to create readwritesplit configs fail after restart
* [MXS-1459](https://jira.mariadb.org/browse/MXS-1459) Binlog checksum default value is wrong if a slave connects with checksum = NONE before master registration or master is not accessible at startup
* [MXS-1457](https://jira.mariadb.org/browse/MXS-1457) Deleted servers are not ignored when users are loaded
* [MXS-1456](https://jira.mariadb.org/browse/MXS-1456) OOM when script variable is empty
* [MXS-1451](https://jira.mariadb.org/browse/MXS-1451) Password is not stored with skip_authentication=true
* [MXS-1450](https://jira.mariadb.org/browse/MXS-1450) Maxadmin commands with a leading space are silently ignored
* [MXS-1449](https://jira.mariadb.org/browse/MXS-1449) Database change not allowed
* [MXS-1163](https://jira.mariadb.org/browse/MXS-1163) Log flood using binlog server on Ubuntu Yakkety Yak

## Packaging

RPM and Debian packages are provided for the Linux distributions supported by
MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is maxscale-X.Y.Z.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
