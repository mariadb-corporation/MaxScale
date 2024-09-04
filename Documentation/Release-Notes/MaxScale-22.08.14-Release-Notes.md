# MariaDB MaxScale 22.08.14 Release Notes -- 2024-09-09

Release 22.08.14 is a GA release.

This document describes the changes in release 22.08.14, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-22.08.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-5234](https://jira.mariadb.org/browse/MXS-5234) webpack warns about yargs
* [MXS-5232](https://jira.mariadb.org/browse/MXS-5232) Large batches of session commands may leave sessions alive for a long time
* [MXS-5227](https://jira.mariadb.org/browse/MXS-5227) MaxScale does not drop supplementary groups if --user is used
* [MXS-5226](https://jira.mariadb.org/browse/MXS-5226) LICENSE.TXT is a dangling symlink in RPMs
* [MXS-5213](https://jira.mariadb.org/browse/MXS-5213) Erroneous "Cluster gtid domain is unknown" error message during failover
* [MXS-5210](https://jira.mariadb.org/browse/MXS-5210) Text could be selected during server node drag for switch-over
* [MXS-5209](https://jira.mariadb.org/browse/MXS-5209) Reads with max_slave_connections=0 after a switchover do not discard stale connections
* [MXS-5200](https://jira.mariadb.org/browse/MXS-5200) CMake 3.28.3 warnings
* [MXS-5198](https://jira.mariadb.org/browse/MXS-5198) Default logrotate config in .deb / docu missing params
* [MXS-5196](https://jira.mariadb.org/browse/MXS-5196) /maxscale/logs/data may return no data if maxlog=0 and syslog=1
* [MXS-5193](https://jira.mariadb.org/browse/MXS-5193) Multi-statement commands may end up being stored in the session command history
* [MXS-5191](https://jira.mariadb.org/browse/MXS-5191) Two cache filters in same service causes errors on session creation
* [MXS-5190](https://jira.mariadb.org/browse/MXS-5190) dotnet EntityFrameworkCore generates insert queries that are getting routed to all nodes as session write
* [MXS-5171](https://jira.mariadb.org/browse/MXS-5171) MaxScale does not have time to open the file during rotation for a new binlog
* [MXS-5162](https://jira.mariadb.org/browse/MXS-5162) Post reboot binlog router entered stuck state
* [MXS-5160](https://jira.mariadb.org/browse/MXS-5160) postinst script prints output while installing
* [MXS-5154](https://jira.mariadb.org/browse/MXS-5154) Adding or removing objects resets the GUI configuration graph's zoom ratio
* [MXS-5146](https://jira.mariadb.org/browse/MXS-5146) 23.08.6 build ppc64le fails
* [MXS-5133](https://jira.mariadb.org/browse/MXS-5133) Memory leak in namedserverfilter
* [MXS-5131](https://jira.mariadb.org/browse/MXS-5131) comment filter uses the wrong module name
* [MXS-5127](https://jira.mariadb.org/browse/MXS-5127) DEALLOCATE PREPARE is not routed to all nodes
* [MXS-5121](https://jira.mariadb.org/browse/MXS-5121) MaxScale detects wrong server character set
* [MXS-4605](https://jira.mariadb.org/browse/MXS-4605) Monitor should drop the connection when faced with an Access Denied error

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
