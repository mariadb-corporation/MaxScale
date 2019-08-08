# MariaDB MaxScale 2.3.10 Release Notes -- 2019-08-01

Release 2.3.10 is a GA release.

This document describes the changes in release 2.3.10, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2613](https://jira.mariadb.org/browse/MXS-2613) Fix cachefilter diagnostics
* [MXS-2607](https://jira.mariadb.org/browse/MXS-2607) Unexpected trailing spaces with --tsv option in MaxCtrl
* [MXS-2606](https://jira.mariadb.org/browse/MXS-2606) Users are loaded from the first available server
* [MXS-2605](https://jira.mariadb.org/browse/MXS-2605) debug assert at readwritesplit.cc:418 failed: a.second.total == a.second.read + a.second.write
* [MXS-2598](https://jira.mariadb.org/browse/MXS-2598) memory leak on handling COM_CHANGE_USER
* [MXS-2597](https://jira.mariadb.org/browse/MXS-2597) MaxScale doesn't handle errors from microhttpd
* [MXS-2594](https://jira.mariadb.org/browse/MXS-2594) Enabling use_priority for not set priority on server level triggers an election
* [MXS-2587](https://jira.mariadb.org/browse/MXS-2587) mxs1507_trx_replay: debug assert in routeQuery
* [MXS-2586](https://jira.mariadb.org/browse/MXS-2586) user_refresh_time default value is wrong
* [MXS-2559](https://jira.mariadb.org/browse/MXS-2559) Log doesn't tell from which server users are loaded from
* [MXS-2520](https://jira.mariadb.org/browse/MXS-2520) Readwritesplit won't connect to master for reads
* [MXS-2502](https://jira.mariadb.org/browse/MXS-2502) Specifying 'information_schema' as default schema upon connection gives 'access denied'
* [MXS-2490](https://jira.mariadb.org/browse/MXS-2490) Unknown prepared statement handler (0) given to mysqld_stmt_execute
* [MXS-2486](https://jira.mariadb.org/browse/MXS-2486) MaxScale 2.3.6 received fatal signal 11
* [MXS-2449](https://jira.mariadb.org/browse/MXS-2449) Maxadmin shows wrong monitor status
* [MXS-2261](https://jira.mariadb.org/browse/MXS-2261) maxkeys overwrites existing key without warning
* [MXS-1901](https://jira.mariadb.org/browse/MXS-1901) Multi continues COM_STMT_SEND_LONG_DATA route to different backends

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
