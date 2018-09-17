# MariaDB MaxScale 2.2.14 Release Notes -- 2018-09-17

Release 2.2.14 is a GA release.

This document describes the changes in release 2.2.14, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-2039](https://jira.mariadb.org/browse/MXS-2039) Filter tables in avrorouter

## Bug fixes

* [MXS-2046](https://jira.mariadb.org/browse/MXS-2046) Memory leak in binlog router
* [MXS-2042](https://jira.mariadb.org/browse/MXS-2042) Hang with multistatment queries
* [MXS-2041](https://jira.mariadb.org/browse/MXS-2041) Crash on failure to create schemarouter session
* [MXS-2040](https://jira.mariadb.org/browse/MXS-2040) Default monitor timeouts are too short
* [MXS-2037](https://jira.mariadb.org/browse/MXS-2037) % wildcards not working with source in Named Server Filter
* [MXS-2036](https://jira.mariadb.org/browse/MXS-2036) A slave with sql thread stopped causes wrong master after failover
* [MXS-2035](https://jira.mariadb.org/browse/MXS-2035) available_when_donor don't working with mariabackup sst method
* [MXS-2034](https://jira.mariadb.org/browse/MXS-2034) query_retry_timeout was not set
* [MXS-2033](https://jira.mariadb.org/browse/MXS-2033) MASTER_SSL_KEY and MASTER_SSL_CERT should not be required
* [MXS-2027](https://jira.mariadb.org/browse/MXS-2027) LOAD DATA LOCAL INFILE is not ignored by protocol modules
* [MXS-2024](https://jira.mariadb.org/browse/MXS-2024) Crash in reauthenticate_client
* [MXS-2019](https://jira.mariadb.org/browse/MXS-2019) All atexit handlers aren't called
* [MXS-2015](https://jira.mariadb.org/browse/MXS-2015) CDC Connector ignores errors after registration is successful
* [MXS-2007](https://jira.mariadb.org/browse/MXS-2007) Fatal: MaxScale 2.2.13 received fatal signal 11 (Aurora Monitor)
* [MXS-1999](https://jira.mariadb.org/browse/MXS-1999) Invalid null relationship handling in REST API 
* [MXS-1996](https://jira.mariadb.org/browse/MXS-1996) Avrorouter writes misleading error messages to the log
* [MXS-1947](https://jira.mariadb.org/browse/MXS-1947) Composite roles are not supported
* [MXS-1880](https://jira.mariadb.org/browse/MXS-1880) MaxScale 2.2.5-1 Crashes after a non clean Start
* [MXS-1736](https://jira.mariadb.org/browse/MXS-1736) Clarify the usage of maxpasswd in documentation
* [MXS-1735](https://jira.mariadb.org/browse/MXS-1735) Clarify SSL documentation

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for supported the Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/mariadb-tx/maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
