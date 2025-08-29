# MariaDB MaxScale 23.02.15 Release Notes

Release 23.02.15 is a GA release.

This document describes the changes in release 23.02.15, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-23.02.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-5606](https://jira.mariadb.org/browse/MXS-5606) Add MAXSCALE_USER and MAXSCALE_PASSWORD environment variables

## Bug fixes

* [MXS-5920](https://jira.mariadb.org/browse/MXS-5920) MaxCtrl/REST-API should tell if password encryption is enabled
* [MXS-5915](https://jira.mariadb.org/browse/MXS-5915) MaxScale 24.02.6 is not compatible with MariaDB 12.0.2: Monitor fails with 'Cannot convert field ON to boolean'
* [MXS-5911](https://jira.mariadb.org/browse/MXS-5911) Upgrade Connector/C to 3.3.16 and 3.4.6
* [MXS-5910](https://jira.mariadb.org/browse/MXS-5910) Error 1047 when trying to run semi-sync replication through Maxscale
* [MXS-5897](https://jira.mariadb.org/browse/MXS-5897) Failing COM_STMT_PREPAREs that don't generate an ID aren't discarded from the history
* [MXS-5865](https://jira.mariadb.org/browse/MXS-5865) Run-time modification of users_refresh_time and users_refresh_interval are not immediately taken into use
* [MXS-5837](https://jira.mariadb.org/browse/MXS-5837) MaxScale 24.02.5 crashes with fatal signal 6 (std::length_error in maxbase::load_file<std::string>) on Ubuntu 24.04
* [MXS-5769](https://jira.mariadb.org/browse/MXS-5769) Watchdog timeout diagnostics are not detailed enough
* [MXS-5760](https://jira.mariadb.org/browse/MXS-5760) Authentication failure on all backends does not immediately close sessions
* [MXS-5733](https://jira.mariadb.org/browse/MXS-5733) MaxScale allows unlimited COM_CHANGE_USER attempts
* [MXS-5703](https://jira.mariadb.org/browse/MXS-5703) List of read-only builtin functions is out-of-date
* [MXS-5673](https://jira.mariadb.org/browse/MXS-5673) Config sync does not mark resources as modified
* [MXS-5653](https://jira.mariadb.org/browse/MXS-5653) MaxScale 24.02.6 GUI does not reflect parameter changes from Configuration Synchronization
* [MXS-5641](https://jira.mariadb.org/browse/MXS-5641) Confusing log message when the wrong connection holds on to the master server lock
* [MXS-5635](https://jira.mariadb.org/browse/MXS-5635) The variable disk_space_threshold should be dynamic for Servers
* [MXS-5559](https://jira.mariadb.org/browse/MXS-5559) maxctrl does not obfuscate password in ps output

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
