# MariaDB MaxScale 23.02.3 Release Notes -- 2023-08-07

Release 23.02.3 is a GA release.

This document describes the changes in release 23.02.3, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-23.02.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-4541](https://jira.mariadb.org/browse/MXS-4541) Provide a way to show details about all supported MaxScale modules via REST API and/or MaxCtrl

## Bug fixes

* [MXS-4684](https://jira.mariadb.org/browse/MXS-4684) Detect ALTER EVENT failure on MariaDB 11.0
* [MXS-4683](https://jira.mariadb.org/browse/MXS-4683) ssl parameters specified on the bootstrap server are not copied to the rest
* [MXS-4680](https://jira.mariadb.org/browse/MXS-4680) Session idleness diagnostic is wrong
* [MXS-4676](https://jira.mariadb.org/browse/MXS-4676) REST-API documentation is wrong about which server parameters can be modified
* [MXS-4672](https://jira.mariadb.org/browse/MXS-4672) Document grants needed for MariaDB 11.1
* [MXS-4666](https://jira.mariadb.org/browse/MXS-4666) causal_reads=local is serialized as causal_reads=true
* [MXS-4665](https://jira.mariadb.org/browse/MXS-4665) Listener creation error is misleading
* [MXS-4664](https://jira.mariadb.org/browse/MXS-4664) xpandmon diagnostics are not useful
* [MXS-4661](https://jira.mariadb.org/browse/MXS-4661) Document supported wire protocol versions
* [MXS-4659](https://jira.mariadb.org/browse/MXS-4659) Cache filter hangs if statement consists of multiple packets.
* [MXS-4658](https://jira.mariadb.org/browse/MXS-4658) Post reboot binlog router entered stuck state
* [MXS-4657](https://jira.mariadb.org/browse/MXS-4657) Add human readable message text to API errors like 404
* [MXS-4656](https://jira.mariadb.org/browse/MXS-4656) Setting session_track_trx_state=true leads to OOM kiled.
* [MXS-4655](https://jira.mariadb.org/browse/MXS-4655) Missing kafkaimporter documentation
* [MXS-4651](https://jira.mariadb.org/browse/MXS-4651) Documentation claims that netmask support is limited to numbers 255 and 0
* [MXS-4648](https://jira.mariadb.org/browse/MXS-4648) MongoDB monitoring promoted when connecting to NoSQL service
* [MXS-4645](https://jira.mariadb.org/browse/MXS-4645) qlafilter log event notifications are sometimes lost
* [MXS-4643](https://jira.mariadb.org/browse/MXS-4643) GUI is unable to create a listener with other protocols than MariaDBProtocol
* [MXS-4634](https://jira.mariadb.org/browse/MXS-4634) readconnroute documentation page contains a typo "max_slave_replication_lag"
* [MXS-4631](https://jira.mariadb.org/browse/MXS-4631) Harden BLR binlog file-index handling
* [MXS-4628](https://jira.mariadb.org/browse/MXS-4628) Connection in Query Editor is closed after 1 hour of being idle
* [MXS-4613](https://jira.mariadb.org/browse/MXS-4613) binlogrouter shows MaxScale's binary log coordinates in SHOW SLAVE STATUS

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
