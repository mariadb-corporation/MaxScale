# MariaDB MaxScale 2.2.9 Release Notes -- 2018-06-06

Release 2.2.9 is a GA release.

This document describes the changes in release 2.2.9, when compared to
release 2.2.7 (2.2.8 was not officially released).

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Bug fixes

### 2.2.9
* [MXS-1899](https://jira.mariadb.org/browse/MXS-1899) generated [maxscale] section causes errors
* [MXS-1896](https://jira.mariadb.org/browse/MXS-1896) LOAD DATA INFILE is mistaken for LOAD DATA LOCAL INFILE
* [MXS-1743](https://jira.mariadb.org/browse/MXS-1743) Maxscale unable to enforce round-robin between read service for Slave

### 2.2.8
* [MXS-1889](https://jira.mariadb.org/browse/MXS-1889) A single remaining master is valid for readconnroute configured with 'router_options=slave'
* [MXS-1740](https://jira.mariadb.org/browse/MXS-1740) Hintfilter leaks memory

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/downloads/mariadb-tx/maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is X.Y.Z. Further, *master* always refers to the latest released non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
