
# MariaDB MaxScale 1.4.1 Release Notes

Release 1.4.1 is a GA release.

This document describes the changes in release 1.4.1, when compared to
release [1.4.0](MaxScale-1.4.0-Release-Notes.md).

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 1.4.0.](https://jira.mariadb.org/browse/MXS-646?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20resolution%20in%20(Fixed%2C%20Done)%20AND%20fixVersion%20%3D%201.4.1)

 * [MXS-646](https://jira.mariadb.org/browse/MXS-646): Namedserverfilter ignores user and source parameters
 * [MXS-632](https://jira.mariadb.org/browse/MXS-632): Replace or update VERSION
 * [MXS-630](https://jira.mariadb.org/browse/MXS-630): Requirement of tables_priv access not documented in "Upgrading" guide
 * [MXS-629](https://jira.mariadb.org/browse/MXS-629): Lack of tables_priv privilege causes confusing error message
 * [MXS-627](https://jira.mariadb.org/browse/MXS-627): Failure to connect to MaxScale with MariaDB Connector/J
 * [MXS-585](https://jira.mariadb.org/browse/MXS-585): Intermittent connection failure with MaxScale 1.2/1.3 using MariaDB/J 1.3

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
is X.Y.Z. Further, *master* always refers to the latest released non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
