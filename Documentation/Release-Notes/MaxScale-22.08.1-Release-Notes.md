# MariaDB MaxScale 22.08.1 Release Notes -- 2022-09-12

Release 22.08.1 is a GA release.

This document describes the changes in release 22.08.1, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-22.08.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-4242](https://jira.mariadb.org/browse/MXS-4242) At startup nosqlprotcol should optionally create a default NoSQL user.

## Bug fixes

* [MXS-4274](https://jira.mariadb.org/browse/MXS-4274) maxctrl reload tls does not reload JWT signature keys
* [MXS-4272](https://jira.mariadb.org/browse/MXS-4272) Certain NoSQL $rename operations can cause a crash
* [MXS-4269](https://jira.mariadb.org/browse/MXS-4269) UPDATE with user variable modification is treated as a session command
* [MXS-4260](https://jira.mariadb.org/browse/MXS-4260) Maxscale crashes frequently while performing load testing
* [MXS-4250](https://jira.mariadb.org/browse/MXS-4250) A non-admin user cannot view its own information
* [MXS-4249](https://jira.mariadb.org/browse/MXS-4249) When creating user, the NoSQL 'userAdminAnyDatabase' role is not handled properly
* [MXS-4240](https://jira.mariadb.org/browse/MXS-4240) MXS-4239 readconnroute module routing read queries to inconsistent slave node
* [MXS-4239](https://jira.mariadb.org/browse/MXS-4239) Maxscale shows replication  status  as [Slave, Running] even when replication credentials are wrong
* [MXS-4237](https://jira.mariadb.org/browse/MXS-4237) Schemarouter duble free crash
* [MXS-4231](https://jira.mariadb.org/browse/MXS-4231) Sometimes the date and time are missing in the query log
* [MXS-4219](https://jira.mariadb.org/browse/MXS-4219) Settings of bootstrap servers are not correctly propagated to dynamic servers
* [MXS-4196](https://jira.mariadb.org/browse/MXS-4196) Readconnroute load balancing behavior is not well documented
* [MXS-4094](https://jira.mariadb.org/browse/MXS-4094) MaxScale failed to login mysql user with empty password

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
