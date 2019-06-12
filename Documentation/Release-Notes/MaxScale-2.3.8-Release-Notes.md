# MariaDB MaxScale 2.3.8 Release Notes -- 2019-06-12

Release 2.3.8 is a GA release.

This document describes the changes in release 2.3.8, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2541](https://jira.mariadb.org/browse/MXS-2541) Crash with transaction_replay=true
* [MXS-2538](https://jira.mariadb.org/browse/MXS-2538) MaxScale sends wrong charset in some cases
* [MXS-2536](https://jira.mariadb.org/browse/MXS-2536) Hang on KILL /*QUERY*/ 1
* [MXS-2525](https://jira.mariadb.org/browse/MXS-2525) before upgrade to 2.3, all works well ,after upgrade maxscale to 2.3.7，some programing drivers can not work well
* [MXS-2520](https://jira.mariadb.org/browse/MXS-2520) Readwritesplit won't connect to master for reads
* [MXS-2511](https://jira.mariadb.org/browse/MXS-2511) Maxscale c-connector version
* [MXS-2507](https://jira.mariadb.org/browse/MXS-2507) Hang on CREATE TABLE tab
* [MXS-2496](https://jira.mariadb.org/browse/MXS-2496) Service user with roles causes false warnings
* [MXS-2494](https://jira.mariadb.org/browse/MXS-2494) MySQLAuth load users query doesn't check mysql.user's plugin column for MariaDB 10.1+
* [MXS-2491](https://jira.mariadb.org/browse/MXS-2491) Destructor dependency between different components of the log
* [MXS-2479](https://jira.mariadb.org/browse/MXS-2479) Don't throw error for PAM_TEXT_INFO in PAM conversation function
* [MXS-2473](https://jira.mariadb.org/browse/MXS-2473) Clarify documentation on regex-related options
* [MXS-2464](https://jira.mariadb.org/browse/MXS-2464) Crash in route_stored_query with ReadWriteSplit
* [MXS-2250](https://jira.mariadb.org/browse/MXS-2250) DESCRIBE on temporary table is routed to slave
* [MXS-2083](https://jira.mariadb.org/browse/MXS-2083) Maxadmin list servers gives a negative for the number of connections
* [MXS-1851](https://jira.mariadb.org/browse/MXS-1851) Using backend protocol as client protocol causes a crash
* [MXS-1700](https://jira.mariadb.org/browse/MXS-1700) Using MariaDBClient for backend protocol module causes a crash

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
