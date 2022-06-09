# MariaDB MaxScale 6.4.0 Release Notes -- 2022-06-09

Release 6.4.0 is a GA release.

This document describes the changes in release 6.4.0, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4152](https://jira.mariadb.org/browse/MXS-4152) Schemarouter performance degrades as the number of tables increases
* [MXS-4151](https://jira.mariadb.org/browse/MXS-4151) Schemarouter duplicate checks are excessively slow
* [MXS-4139](https://jira.mariadb.org/browse/MXS-4139) connection_keepalive sends pings even if client is idle
* [MXS-4138](https://jira.mariadb.org/browse/MXS-4138) Race condition in binlogrouter
* [MXS-4134](https://jira.mariadb.org/browse/MXS-4134) /etc/maxscale.cnf.d/ is not created by package installation
* [MXS-4115](https://jira.mariadb.org/browse/MXS-4115) Maxscale prints user/pass with CHANGE MASTER command in logfile while failover.
* [MXS-4113](https://jira.mariadb.org/browse/MXS-4113) namedserverfilter does not work with targets parameter
* [MXS-4105](https://jira.mariadb.org/browse/MXS-4105) Queries on already established connections hanging for 15min when Redis server disconnected hard
* [MXS-4100](https://jira.mariadb.org/browse/MXS-4100) connection_keepalive=0 causes a memory leak

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
