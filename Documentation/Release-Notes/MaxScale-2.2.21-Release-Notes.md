# MariaDB MaxScale 2.2.21 Release Notes -- 2019-05-08

Release 2.2.21 is a GA release.

This document describes the changes in release 2.2.21, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2410](https://jira.mariadb.org/browse/MXS-2410) Hangup delivered to wrong DCB
* [MXS-2366](https://jira.mariadb.org/browse/MXS-2366) Wrong tarball RPATH

## Changes to MariaDB-Monitor failover

Failover is no longer disabled permanently if it or any other cluster operation fails.
The disabling is now only temporary and lasts for 'failcount' monitor iterations. Check
[MariaDB-Monitor documentation](../Monitors/MariaDB-Monitor.md#limitations-and-requirements)
for more information.

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for supported the Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/mariadb-tx/maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
