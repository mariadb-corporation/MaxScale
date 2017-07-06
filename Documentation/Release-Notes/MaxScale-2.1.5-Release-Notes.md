# MariaDB MaxScale 2.1.5 Release Notes

Release 2.1.5 is a GA release.

This document describes the changes in release 2.1.5, when compared to
release [2.1.4](MaxScale-2.1.4-Release-Notes.md).

If you are upgrading from release 2.0, please also read the following
release notes:
[2.1.4](./MaxScale-2.1.4-Release-Notes.md)
[2.1.3](./MaxScale-2.1.3-Release-Notes.md)
[2.1.2](./MaxScale-2.1.2-Release-Notes.md)
[2.1.1](./MaxScale-2.1.1-Release-Notes.md)
[2.1.0](./MaxScale-2.1.0-Release-Notes.md)

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Schemarouter

Starting with MaxScale 2.1.5, the _schemarouter_ will prioritize the current
database over an explicit database if tables in the the current database are
used in a query.

## New Features

### Schemarouter

A new parameter for the _schemarouter_ was added that allows deterministic
resolution of database mapping conflicts (i.e. the database exists on more than
one backend server).

The new `preferred_server` parameter takes a server name as its value. If a
database mapping conflict occurs, the server given as the parameter will have
preference. In practice, this means that databases on a central server can be
replicated to the shards for doing JOINs but writes to the replicate database
will still go to the central database.

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 2.1.4.]
(https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.1.5)

* [MXS-1310](https://jira.mariadb.org/browse/MXS-1310) schemarouter ignores local copy of duplicate schemas on JOIN

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
is maxscale-X.Y.Z.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
