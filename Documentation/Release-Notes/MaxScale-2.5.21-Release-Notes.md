# MariaDB MaxScale 2.5.21 Release Notes

Release 2.5.21 is a GA release.

This document describes the changes in release 2.5.21, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-2904](https://jira.mariadb.org/browse/MXS-2904) Document MaxScale performance tuning

## Bug fixes

* [MXS-4181](https://jira.mariadb.org/browse/MXS-4181) MaxScale w/SSL doesn't work on FIPS RHEL7
* [MXS-4166](https://jira.mariadb.org/browse/MXS-4166) Filter diagnostics are not shown in `maxctrl show filters`
* [MXS-4165](https://jira.mariadb.org/browse/MXS-4165) Servers with priority=0 are selected as Master
* [MXS-4164](https://jira.mariadb.org/browse/MXS-4164) Debug assertion when cat session ends
* [MXS-4160](https://jira.mariadb.org/browse/MXS-4160) Maxscale galeramon  + max_slave_replication_lag = Could not find valid server for target type TARGET_SLAVE
* [MXS-4152](https://jira.mariadb.org/browse/MXS-4152) Schemarouter performance degrades as the number of tables increases
* [MXS-4151](https://jira.mariadb.org/browse/MXS-4151) Schemarouter duplicate checks are excessively slow
* [MXS-4146](https://jira.mariadb.org/browse/MXS-4146) Xpand MaxScale Tutorial in KB doesn't work
* [MXS-4141](https://jira.mariadb.org/browse/MXS-4141) connection_keepalive=0 causes a memory leak
* [MXS-4139](https://jira.mariadb.org/browse/MXS-4139) connection_keepalive sends pings even if client is idle
* [MXS-4138](https://jira.mariadb.org/browse/MXS-4138) Race condition in binlogrouter
* [MXS-4134](https://jira.mariadb.org/browse/MXS-4134) /etc/maxscale.cnf.d/ is not created by package installation
* [MXS-4132](https://jira.mariadb.org/browse/MXS-4132) router_options=master ignores rank for first server
* [MXS-4127](https://jira.mariadb.org/browse/MXS-4127) MaxCtrl: list services does not include other targets
* [MXS-4121](https://jira.mariadb.org/browse/MXS-4121) MaxCtrl is limited to 2GB of memory
* [MXS-4120](https://jira.mariadb.org/browse/MXS-4120) Avrorouter crash with a SEQUENCE engine table
* [MXS-4115](https://jira.mariadb.org/browse/MXS-4115) Maxscale prints user/pass with CHANGE MASTER command in logfile while failover.
* [MXS-4110](https://jira.mariadb.org/browse/MXS-4110) Schemarouter does not ignore the sys schema
* [MXS-4100](https://jira.mariadb.org/browse/MXS-4100) connection_keepalive=0 causes a memory leak

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
