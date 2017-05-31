# MariaDB MaxScale 2.1.3 Release Notes -- 2017-05-23

Release 2.1.3 is a GA release.

This document describes the changes in release 2.1.3, when compared to
release [2.1.2](MaxScale-2.1.2-Release-Notes.md).

If you are upgrading from release 2.0, please also read the following
release notes:
[2.1.2](./MaxScale-2.1.2-Release-Notes.md)
[2.1.1](./MaxScale-2.1.1-Release-Notes.md)
[2.1.0](./MaxScale-2.1.0-Release-Notes.md)

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## MariaDB 10.2

MaxScale 2.1 has not been extended to understand all new features that
MariaDB 10.2 introduces. Please see
[Support for 10.2](../About/Support-for-10.2.md)
for details.

## Changed Features

### Cache

* The storage `storage_rocksdb` is no longer built by default and is
  not included in the MariaDB MaxScale package.

### Maxrows

* It can now be specified whether the _maxrows_ filter should return an
  empty resultset, an error packet or an ok packet, when the limit has
  been reached.

  Please refer to the
  [maxrows documentation](../Filters/Maxrows.md)
  for details.

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 2.1.2.](https://jira.mariadb.org/browse/MXS-1212?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20resolution%20in%20(Fixed%2C%20Done)%20AND%20fixVersion%20%3D%202.1.3)

* [MXS-1263](https://jira.mariadb.org/browse/MXS-1263) broken TCP connections are not always cleaned properly
* [MXS-1244](https://jira.mariadb.org/browse/MXS-1244) MySQL monitor "detect_replication_lag=true" doesn't work with "mysql51_replication=true"
* [MXS-1227](https://jira.mariadb.org/browse/MXS-1227) Nagios Plugins broken by change in output of "show monitors" in 2.1
* [MXS-1221](https://jira.mariadb.org/browse/MXS-1221) Nagios plugin scripts does not process -S option properly
* [MXS-1213](https://jira.mariadb.org/browse/MXS-1213) Improve documentation of dynamic configuration changes
* [MXS-1212](https://jira.mariadb.org/browse/MXS-1212) Excessive execution time when maxrows limit has been reached
* [MXS-1202](https://jira.mariadb.org/browse/MXS-1202) maxadmin "show service" counters overflow
* [MXS-1200](https://jira.mariadb.org/browse/MXS-1200) config file lines limited to ~1024 chars

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is X.Y.Z.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
