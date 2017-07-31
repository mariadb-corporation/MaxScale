# MariaDB MaxScale 2.1.5 Release Notes -- 2017-07-31

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

### SSL CA Certificates

Before MaxScale 2.1.5, MaxScale would only use the first certificate file found
in the CA certificate file. In MaxScale 2.1.5, the first certificate is loaded
and the rest of the certificates on the file are stored in the chain store.

This change should not cause any changes in MaxScale's behavior.

### `root_node_as_master`

The galeramon parameter `root_node_as_master` is now disabled by default. The
option should be enabled when it is of great importance to know that all
MaxScale instances treat a shared Galera cluster in the same way.

### Schemarouter

Starting with MaxScale 2.1.5, the _schemarouter_ will prioritize the current
database over an explicit database if tables in the the current database are
used in a query.

### Dbfwfilter

The function type rule will now accept backtick quoted values. This allows
keywords such as `insert` and `function` to be used as values for a function
rule.

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

[Here is a list of bugs fixed in MaxScale 2.1.5.]
(https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.1.5)

* [MXS-1335](https://jira.mariadb.org/browse/MXS-1335) root_node_as_master should not be enabled by default
* [MXS-1330](https://jira.mariadb.org/browse/MXS-1330) insertstream attempts to parse all buffers
* [MXS-1329](https://jira.mariadb.org/browse/MXS-1329) Using filters with SSL and keep alive can cause errors
* [MXS-1328](https://jira.mariadb.org/browse/MXS-1328) Strange behavior with routes between master / slaves
* [MXS-1326](https://jira.mariadb.org/browse/MXS-1326) Upgrade error on Ubuntu Xenial
* [MXS-1324](https://jira.mariadb.org/browse/MXS-1324) MaxScale 2.1.4 compiled without the avrorouter?
* [MXS-1323](https://jira.mariadb.org/browse/MXS-1323) Maxscale2.1.3 coredump
* [MXS-1319](https://jira.mariadb.org/browse/MXS-1319) Maxscale selecting extra whitespace while loading users
* [MXS-1318](https://jira.mariadb.org/browse/MXS-1318) Use SSL_CTX_use_certificate_chain_file in Maxscale to use CA signed certificates
* [MXS-1316](https://jira.mariadb.org/browse/MXS-1316) error using Kafka with binlog router
* [MXS-1313](https://jira.mariadb.org/browse/MXS-1313) Character set is not updated if servers are down
* [MXS-1312](https://jira.mariadb.org/browse/MXS-1312) Rule with only on_queries do not work
* [MXS-1311](https://jira.mariadb.org/browse/MXS-1311) Function type rule that blocks function results in syntax error
* [MXS-1310](https://jira.mariadb.org/browse/MXS-1310) schemarouter ignores local copy of duplicate schemas on JOIN
* [MXS-1309](https://jira.mariadb.org/browse/MXS-1309) ALTER TABLE detection is broken
* [MXS-1285](https://jira.mariadb.org/browse/MXS-1285) cannot stat `/usr/share/maxscale/upstart/maxscale.conf': No such file or directory

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
