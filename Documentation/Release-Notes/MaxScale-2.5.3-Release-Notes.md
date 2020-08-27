# MariaDB MaxScale 2.5.3 Release Notes

Release 2.5.3 is a GA release.

This document describes the changes in release 2.5.3, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-3111](https://jira.mariadb.org/browse/MXS-3111) MaxGui should show what module a monitor is using
* [MXS-3095](https://jira.mariadb.org/browse/MXS-3095) kafkaCDC DML events not include table_schema,table_name information
* [MXS-3071](https://jira.mariadb.org/browse/MXS-3071) Display filters and listeners on the dashboard page of MaxGui
* [MXS-2615](https://jira.mariadb.org/browse/MXS-2615) Add expire_log_days to BinlogRouter

## Bug fixes

* [MXS-3126](https://jira.mariadb.org/browse/MXS-3126) Crash in RWSplitSession::get_slave_backend
* [MXS-3125](https://jira.mariadb.org/browse/MXS-3125) User account fetch should obey ssl_version and check certificate
* [MXS-3124](https://jira.mariadb.org/browse/MXS-3124) Debug assert with COM_STMT_EXECUTE
* [MXS-3121](https://jira.mariadb.org/browse/MXS-3121) At SIGSEGV time, the statement currently being parsed should be logged.
* [MXS-3120](https://jira.mariadb.org/browse/MXS-3120) Crash in qc_sqlite.
* [MXS-3115](https://jira.mariadb.org/browse/MXS-3115) Error loading kubernetes mounted cnf
* [MXS-3114](https://jira.mariadb.org/browse/MXS-3114) Listener creation via REST API with sockets doesn't work
* [MXS-3113](https://jira.mariadb.org/browse/MXS-3113) No message in new log after rotate
* [MXS-3109](https://jira.mariadb.org/browse/MXS-3109) MaxScale shutdowns after creating a filter using throttlefilter module without assigning throttling_duration value
* [MXS-3105](https://jira.mariadb.org/browse/MXS-3105) maxctrl list servers , master's  gtid is empty , why?
* [MXS-3104](https://jira.mariadb.org/browse/MXS-3104) binlog router Router Diagnostics empty
* [MXS-3103](https://jira.mariadb.org/browse/MXS-3103) Update service relationships with type 'servers' causes all filters of that service being unlinked
* [MXS-3101](https://jira.mariadb.org/browse/MXS-3101) getpeername()' failed on file descriptor
* [MXS-3100](https://jira.mariadb.org/browse/MXS-3100) Potential memory leak
* [MXS-3098](https://jira.mariadb.org/browse/MXS-3098) static assertion failed: duration must be a specialization of std::chrono::duration
* [MXS-3097](https://jira.mariadb.org/browse/MXS-3097) Connection throttling doesn't work
* [MXS-3093](https://jira.mariadb.org/browse/MXS-3093) Client side certificates for secure REST API  fails
* [MXS-3090](https://jira.mariadb.org/browse/MXS-3090) default_value for some types is a string
* [MXS-3089](https://jira.mariadb.org/browse/MXS-3089) Backend not closed on failed session command
* [MXS-3084](https://jira.mariadb.org/browse/MXS-3084) DELETE of services with force=yes doesn't work
* [MXS-3083](https://jira.mariadb.org/browse/MXS-3083) api_key and local_address use bad default values
* [MXS-3081](https://jira.mariadb.org/browse/MXS-3081) interactive mode for maxpasswd
* [MXS-3079](https://jira.mariadb.org/browse/MXS-3079) MaxScale automatically assigns port value as 3306 after creating a server that using socket
* [MXS-3078](https://jira.mariadb.org/browse/MXS-3078) Defer loading of modules until they are needed
* [MXS-3076](https://jira.mariadb.org/browse/MXS-3076) Timers stop working during shutdown
* [MXS-3073](https://jira.mariadb.org/browse/MXS-3073) Type value for /maxscale/modules endpoint is module
* [MXS-3069](https://jira.mariadb.org/browse/MXS-3069) Unnecessary AuthSwitchRequest with old clients

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
