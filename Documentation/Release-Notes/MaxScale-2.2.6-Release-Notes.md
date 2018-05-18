# MariaDB MaxScale 2.2.6 Release Notes -- 2018-05

Release 2.2.6 is a GA release.

This document describes the changes in release 2.2.6, when compared to
release 2.2.5.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## New Features

* It is now possible to configure the MariaDB Monitor to turn
  on the read_only flag of a server it deems is a slave.

## Bug fixes

* [MXS-1874](https://jira.mariadb.org/browse/MXS-1874) SET STATEMENT ... FOR is wrongly classified
* [MXS-1873](https://jira.mariadb.org/browse/MXS-1873) Errors with large session commands
* [MXS-1866](https://jira.mariadb.org/browse/MXS-1866) Prepared statements do not work
* [MXS-1861](https://jira.mariadb.org/browse/MXS-1861) masking filter logs warnings with multistatements
* [MXS-1852](https://jira.mariadb.org/browse/MXS-1852) mysql_client_test test_bug17309863 failed
* [MXS-1847](https://jira.mariadb.org/browse/MXS-1847) Race condition in server_get_parameter
* [MXS-1846](https://jira.mariadb.org/browse/MXS-1846) Wrong packet number in KILL command error
* [MXS-1843](https://jira.mariadb.org/browse/MXS-1843) Sporadic test_logthrottling failures on Ubuntu
* [MXS-1839](https://jira.mariadb.org/browse/MXS-1839) show sessions leaks memory
* [MXS-1837](https://jira.mariadb.org/browse/MXS-1837) Typo in REST API service stop documentation
* [MXS-1836](https://jira.mariadb.org/browse/MXS-1836) MaxInfo show eventTimes returns garbage.
* [MXS-1833](https://jira.mariadb.org/browse/MXS-1833) Connection timeout when creating CONNECT tables for maxinfo in Galera cluster with SSL set up
* [MXS-1832](https://jira.mariadb.org/browse/MXS-1832) maxctrl returns 0 even when errors are returned
* [MXS-1831](https://jira.mariadb.org/browse/MXS-1831) No error on invalid monitor parameter alteration
* [MXS-1830](https://jira.mariadb.org/browse/MXS-1830) Invalid free in Cache test program
* [MXS-1829](https://jira.mariadb.org/browse/MXS-1829) SELECT PREVIOUS VALUE FOR SEC classified differently than SELECT NEXT...
* [MXS-1826](https://jira.mariadb.org/browse/MXS-1826) COM_CHANGE_USER not work with Oracle MySQL
* [MXS-1825](https://jira.mariadb.org/browse/MXS-1825) test_wl4435_3 case from mysql_client_test.c lead to crash
* [MXS-1824](https://jira.mariadb.org/browse/MXS-1824) test_bug11909 case from mysql_client_test.c lead to crash
* [MXS-1772](https://jira.mariadb.org/browse/MXS-1772) Netmask limitations are not documented

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
