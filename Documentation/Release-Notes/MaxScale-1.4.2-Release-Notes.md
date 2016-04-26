
# MariaDB MaxScale 1.4.2 Release Notes

Release 1.4.2 is a GA release.

This document describes the changes in release 1.4.2, when compared to
release 1.4.1.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 1.4.1.](https://jira.mariadb.org/browse/MXS-683?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20resolution%20in%20(Fixed%2C%20Done)%20AND%20fixVersion%20%3D%201.4.2)

 * [MXS-684](https://jira.mariadb.org/browse/MXS-684): Password field still used with MySQL 5.7
 * [MXS-683](https://jira.mariadb.org/browse/MXS-683): qc_mysqlembedded reports as-name instead of original-name.
 * [MXS-681](https://jira.mariadb.org/browse/MXS-681): Loading service users error
 * [MXS-680](https://jira.mariadb.org/browse/MXS-680): qc_mysqlembedded fails to look into function when reporting affected fields
 * [MXS-679](https://jira.mariadb.org/browse/MXS-679): qc_mysqlembedded excludes some fields, when reporting affected fields
 * [MXS-662](https://jira.mariadb.org/browse/MXS-662): No Listener on different IPs but same port since 1.4.0
 * [MXS-661](https://jira.mariadb.org/browse/MXS-661): Log fills with 'Length (0) is 0 or query string allocation failed'
 * [MXS-656](https://jira.mariadb.org/browse/MXS-656): after upgrade from 1.3 to 1.4, selecting master isn't working as expected
 * [MXS-616](https://jira.mariadb.org/browse/MXS-616): Duplicated binlog event under heavy load.

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
