# MariaDB MaxScale 6.4.2 Release Notes

Release 6.4.2 is a GA release.

This document describes the changes in release 6.4.2, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4239](https://jira.mariadb.org/browse/MXS-4239) Maxscale shows replication  status  as [Slave, Running] though replication is broken with Last_IO_Errno: XXXXXX
* [MXS-4237](https://jira.mariadb.org/browse/MXS-4237) Schemarouter duble free crash
* [MXS-4219](https://jira.mariadb.org/browse/MXS-4219) Settings of bootstrap servers are not correctly propagated to dynamic servers
* [MXS-4218](https://jira.mariadb.org/browse/MXS-4218) Configuration synchronization fails if static global parameters are different
* [MXS-4211](https://jira.mariadb.org/browse/MXS-4211) MaxScale throws std::out_of_range on invalid listener parameter
* [MXS-4209](https://jira.mariadb.org/browse/MXS-4209) KILL command doesn't work correctly if persistent connections are enabled
* [MXS-4198](https://jira.mariadb.org/browse/MXS-4198) MaxScale fails to validate its own certificate when the chain of trust is unknown to OpenSSL
* [MXS-4196](https://jira.mariadb.org/browse/MXS-4196) Readconnroute load balancing behavior is not well documented
* [MXS-4183](https://jira.mariadb.org/browse/MXS-4183) Multiplexing fails with "Timed out when waiting for a connection"

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
