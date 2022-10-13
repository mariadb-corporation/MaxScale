# MariaDB MaxScale 6.4.3 Release Notes -- 2022-10-14

Release 6.4.3 is a GA release.

This document describes the changes in release 6.4.3, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4321](https://jira.mariadb.org/browse/MXS-4321) Error from missing --secure option is not helpful
* [MXS-4313](https://jira.mariadb.org/browse/MXS-4313) MaxCtrl misinterprets some arguments
* [MXS-4312](https://jira.mariadb.org/browse/MXS-4312) REST API accepts empty resource IDs
* [MXS-4307](https://jira.mariadb.org/browse/MXS-4307) Parser can't recognize convert function parameters and cause wrong routing decision
* [MXS-4304](https://jira.mariadb.org/browse/MXS-4304) MariaDB-Monitor spams log with connection errors if server is both [Maintenance] and [Down]
* [MXS-4290](https://jira.mariadb.org/browse/MXS-4290) Maxscale masking filter returns parsing error on SELECT with very long WHERE
* [MXS-4289](https://jira.mariadb.org/browse/MXS-4289) Transaction starts on wrong server with autocommit=0
* [MXS-4283](https://jira.mariadb.org/browse/MXS-4283) Race condition in KILL command processing
* [MXS-4282](https://jira.mariadb.org/browse/MXS-4282) Servers that are [Down] may have [Slave of External Server]
* [MXS-4280](https://jira.mariadb.org/browse/MXS-4280) qc_sqlite does not properly handle a LIMIT clause
* [MXS-4279](https://jira.mariadb.org/browse/MXS-4279) "sub" field not set for JWTs
* [MXS-4275](https://jira.mariadb.org/browse/MXS-4275) MaxScale tries to start up if --export-config is used and a cached cluster configuration is present
* [MXS-4269](https://jira.mariadb.org/browse/MXS-4269) UPDATE with user variable modification is treated as a session command
* [MXS-4267](https://jira.mariadb.org/browse/MXS-4267) NULL values are exported as empty strings when using CSV format
* [MXS-4260](https://jira.mariadb.org/browse/MXS-4260) Maxscale crashes frequently while performing load testing
* [MXS-4259](https://jira.mariadb.org/browse/MXS-4259) warning: [xpandmon] 'late' is an unknown sub-state for a Xpand node
* [MXS-4247](https://jira.mariadb.org/browse/MXS-4247) Listener created with encryption even if ssl=false is passed
* [MXS-4231](https://jira.mariadb.org/browse/MXS-4231) Sometimes the date and time are missing in the query log
* [MXS-4227](https://jira.mariadb.org/browse/MXS-4227) MaxCtrl incompatibility with MemoryDenyWriteExecute=true is not documented
* [MXS-4221](https://jira.mariadb.org/browse/MXS-4221) GUI does not show other services used by services
* [MXS-4156](https://jira.mariadb.org/browse/MXS-4156) Update documentation on required monitor privileges
* [MXS-4094](https://jira.mariadb.org/browse/MXS-4094) Allow empty token when client is replying to AuthSwitchRequest
* [MXS-4083](https://jira.mariadb.org/browse/MXS-4083) CPU utilization high on MaxScale host

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the supported Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/#mariadb_platform-mariadb_maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
