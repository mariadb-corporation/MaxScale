# MariaDB MaxScale 2.4.8 Release Notes -- 2020-03-18

Release 2.4.8 is a GA release.

This document describes the changes in release 2.4.8, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2921](https://jira.mariadb.org/browse/MXS-2921) Memory leak in `alter maxscale`
* [MXS-2919](https://jira.mariadb.org/browse/MXS-2919) max_slave_replication_lag uses servers with unknown replication lag
* [MXS-2917](https://jira.mariadb.org/browse/MXS-2917) qc_sqlite leaks memory with complex CREATE TABLE query
* [MXS-2907](https://jira.mariadb.org/browse/MXS-2907) Logrotate warnings when PID file does not exist
* [MXS-2898](https://jira.mariadb.org/browse/MXS-2898) warning: The query can't be routed to all backend servers because it includes SELECT and SQL variable modifications which is not supported.
* [MXS-2893](https://jira.mariadb.org/browse/MXS-2893) UnhandledPromiseRejectionWarning when creating a new filter
* [MXS-2878](https://jira.mariadb.org/browse/MXS-2878) Monitor connections do not insist on SSL being used
* [MXS-2873](https://jira.mariadb.org/browse/MXS-2873) Router diagnostic output is not documented
* [MXS-2844](https://jira.mariadb.org/browse/MXS-2844) maxctrl fails to destroy binlogrouter service
* [MXS-2811](https://jira.mariadb.org/browse/MXS-2811) Cannot configure Connector-C TLS version
* [MXS-2508](https://jira.mariadb.org/browse/MXS-2508) Using DIV function in query produces an error
* [MXS-2382](https://jira.mariadb.org/browse/MXS-2382) TLS/SSL setup is not a part of the server mini-tutorial
* [MXS-2227](https://jira.mariadb.org/browse/MXS-2227) optimize table: Query could not be tokenized/Parsing the query failed, cannot report query type

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for supported the Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/#mariadb_platform-mariadb_maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
