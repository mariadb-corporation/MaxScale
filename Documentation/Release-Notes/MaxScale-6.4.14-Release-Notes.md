# MariaDB MaxScale 6.4.14 Release Notes

Release 6.4.14 is a GA release.

This document describes the changes in release 6.4.14, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-4862](https://jira.mariadb.org/browse/MXS-4862) ssl_version should specify minimum version

## Bug fixes

* [MXS-4956](https://jira.mariadb.org/browse/MXS-4956) Session commands ignore delayed_retry_timeout
* [MXS-4947](https://jira.mariadb.org/browse/MXS-4947) Tables in information_schema are treated as a normal tables
* [MXS-4945](https://jira.mariadb.org/browse/MXS-4945) GUI doesn't validate object name uniqueness accurately
* [MXS-4934](https://jira.mariadb.org/browse/MXS-4934) Use-after-free after service deletion
* [MXS-4926](https://jira.mariadb.org/browse/MXS-4926) History length of sessions is not visible in the REST-API
* [MXS-4924](https://jira.mariadb.org/browse/MXS-4924) Very fast client and server may end up busy-looping a worker
* [MXS-4923](https://jira.mariadb.org/browse/MXS-4923) The "New messages available" button in the GUI Logs Archive does not disappear after being clicked.
* [MXS-4922](https://jira.mariadb.org/browse/MXS-4922) Memory growth for long-running sessions that use COM_CHANGE_USER
* [MXS-4921](https://jira.mariadb.org/browse/MXS-4921) Memory growth for long-running sessions that use prepared statements
* [MXS-4913](https://jira.mariadb.org/browse/MXS-4913) Memory leak when closing SSL connection
* [MXS-4912](https://jira.mariadb.org/browse/MXS-4912) Query classifier cache total-size book-keeping may be wrong
* [MXS-4910](https://jira.mariadb.org/browse/MXS-4910) readconnroute performance regression in 6.4
* [MXS-4908](https://jira.mariadb.org/browse/MXS-4908) Undefined behavior with module commands that take no arguments
* [MXS-4906](https://jira.mariadb.org/browse/MXS-4906) MonitorWorker::call_run_one_tick() called more often than intended
* [MXS-4900](https://jira.mariadb.org/browse/MXS-4900) maxctrl show qc_cache can easily overwhelm MaxScale
* [MXS-4896](https://jira.mariadb.org/browse/MXS-4896) Reducing the size of the query classifier cache does not cause excess entries to be freed.
* [MXS-4895](https://jira.mariadb.org/browse/MXS-4895) QC cache memory accounting on CentOS 7 is wrong
* [MXS-4865](https://jira.mariadb.org/browse/MXS-4865) 5.5.5- prefix should not be added if all backends are MariaDB 11

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
