# MariaDB MaxScale 6.4.1 Release Notes -- 2022-07-14

Release 6.4.1 is a GA release.

This document describes the changes in release 6.4.1, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4193](https://jira.mariadb.org/browse/MXS-4193) HTTPS requests don't include Path=/ in cookies
* [MXS-4185](https://jira.mariadb.org/browse/MXS-4185) The state of the bootstrap nodes is not updated properly
* [MXS-4181](https://jira.mariadb.org/browse/MXS-4181) MaxScale w/SSL doesn't work on FIPS RHEL7
* [MXS-4180](https://jira.mariadb.org/browse/MXS-4180) Some non-multi-statement queries are classified as multi-statement ones
* [MXS-4177](https://jira.mariadb.org/browse/MXS-4177) maxctrl call command leaves stale errors
* [MXS-4172](https://jira.mariadb.org/browse/MXS-4172) Hang in RWSplitSession::correct_packet_sequence
* [MXS-4171](https://jira.mariadb.org/browse/MXS-4171) Unmodifiable parameters aren't prevented from being modified
* [MXS-4170](https://jira.mariadb.org/browse/MXS-4170) Bad `create monitor` command leaves a ghost monitor
* [MXS-4169](https://jira.mariadb.org/browse/MXS-4169) Listeners created at runtime require ssl_ca_cert when it should not be required
* [MXS-4166](https://jira.mariadb.org/browse/MXS-4166) Filter diagnostics are not shown in `maxctrl show filters`
* [MXS-4165](https://jira.mariadb.org/browse/MXS-4165) Servers with priority=0 are selected as Master
* [MXS-4164](https://jira.mariadb.org/browse/MXS-4164) Debug assertion when cat session ends
* [MXS-4160](https://jira.mariadb.org/browse/MXS-4160) Galeramon doesn't work with max_slave_replication_lag
* [MXS-4148](https://jira.mariadb.org/browse/MXS-4148) Log warning if reverse name resolution takes significant time

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
