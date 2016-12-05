# MariaDB MaxScale 2.0.3 Release Notes

Release 2.0.3 is a GA release.

This document describes the changes in release 2.0.3, when compared to
release [2.0.2](MaxScale-2.0.2-Release-Notes.md).

If you are upgrading from release 1.4.4, please also read the release
notes of release [2.0.0](./MaxScale-2.0.0-Release-Notes.md) and
release [2.0.1](./MaxScale-2.0.1-Release-Notes.md).

For any problems you encounter, please submit a bug report at
[Jira](https://jira.mariadb.org).

## Updated Features

### [MXS-1027] (https://jira.mariadb.org/browse/MXS-1027) Add Upstart support (including respawn) for MaxScale

MaxScale now provides an Upstart configuration file for systems that do not
support systemd.

## Bug fixes

[Here](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.0.3)
is a list of bugs fixed since the release of MaxScale 2.0.1.

* [MXS-1009](https://jira.mariadb.org/browse/MXS-1009): maxinfo sigsegv in spinlock_release

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is derived
from the version of MaxScale. For instance, the tag of version `X.Y.Z` of MaxScale
is `maxscale-X.Y.Z`.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).

