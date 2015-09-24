# MariaDB MaxScale 1.2.1 Release Notes

## 1.2.1 GA

This document details the changes in version 1.2.1 since the release of the 1.2.0 version of MaxScale.

Here is a list of bugs fixed since the release of MaxScale 1.2.0.

* [MXS-380](https://mariadb.atlassian.net/browse/MXS-380): Upgrade overwrites existing /etc/maxscale.cfg
* [MXS-376](https://mariadb.atlassian.net/browse/MXS-376): MaxScale terminates with SIGABRT
* [MXS-374](https://mariadb.atlassian.net/browse/MXS-374): Maxscale is running under 'root' in Ubuntu/Debina
* [MXS-356](https://mariadb.atlassian.net/browse/MXS-356): Connection timeouts for authentication are not configurable
* [MXS-349](https://mariadb.atlassian.net/browse/MXS-349): SSL connections are unstable
* [MXS-339](https://mariadb.atlassian.net/browse/MXS-339): RPM installations overwrite maxscale.cnf
* [MXS-335](https://mariadb.atlassian.net/browse/MXS-335): Crash in readwritesplit
* [MXS-328](https://mariadb.atlassian.net/browse/MXS-328): Read-Write router frees buffer after write (which also frees)
* [MXS-325](https://mariadb.atlassian.net/browse/MXS-325): Maxinfo HTTP interface leaks memory
* [MXS-324](https://mariadb.atlassian.net/browse/MXS-324): HTTPD protocol leaks memory
* [MXS-322](https://mariadb.atlassian.net/browse/MXS-322): Available_when_donor is not working
* [MXS-314](https://mariadb.atlassian.net/browse/MXS-314): Read Write Split Error with Galera Nodes
* [MXS-306](https://mariadb.atlassian.net/browse/MXS-306): User authentication fails when using a large number of users
* [MXS-305](https://mariadb.atlassian.net/browse/MXS-305): Do not build packages into / with make package
* [MXS-270](https://mariadb.atlassian.net/browse/MXS-270): Crash with MySQLBackend protocol module
* [MXS-207](https://mariadb.atlassian.net/browse/MXS-207): MaxScale received fatal signal 11 (libreadwritesplit)

## Known Issues and Limitations

There are a number of limitations within this version of MaxScale. For a complete list of them, please read the [Limitations](../About/Limitations.md) document.

## Packaging

Both RPM and Debian packages are available for MaxScale in addition to the tar based releases previously distributed we now provide

* CentOS/RedHat 5

* CentOS/RedHat 6

* CentOS/RedHat 7

* Debian 6

* Debian 7

* Ubuntu 12.04 LTS

* Ubuntu 14.04 LTS

* SuSE Linux Enterprise 11

* SuSE Linux Enterprise 12
