# MaxScale Release Notes

## 1.1 GA

This document details the changes in version 1.1.1 since the release of the 1.1 GA Release of the MaxScale product.

## New Features

### Improved memory management options

Readwritesplit and schemarouter now both support upper limits to session state modifying commands. They both also allow the complete disabling of the history, making the sessions consume the smallest amount of memory while still making sure all slaves keep identical session states.

### Improved trace logging

The process of the user authentication data retrieval is logged into the trace log and the readconnroute router also outputs more information into the trace log. This allows for easier problem detection and configuration tuning.

### More informative output from maxkeys and maxpasswd

Using the password functionality in MaxScale is now a lot easier. Both programs now produce verbose and exact error messages.

## Bug Fixes

Here is a list of bugs fixed since the release of the 1.1 version of MaxScale. The bug IDs are from the **[Mariadb Jira](https://mariadb.atlassian.net/)**.

|Bug|Description|
|---|-----------|
|[MXS-99](https://mariadb.atlassian.net/browse/MXS-99)|/etc/init.d/maxscale reload doesn't do anything|
|[MXS-83](https://mariadb.atlassian.net/browse/MXS-83)|linkage fails when system pcre library is recent|
|[MXS-112](https://mariadb.atlassian.net/browse/MXS-112)|Disable saving of session commands in the readwritesplit and schemarouter modules|
|[MXS-114](https://mariadb.atlassian.net/browse/MXS-114)|Disable recovery of disconnected slaves|
|[MXS-73](https://mariadb.atlassian.net/browse/MXS-73)|MaxScale uses nearly 100% CPU |
|[MXS-36](https://mariadb.atlassian.net/browse/MXS-36)|bugzillaId-671: wrong message if SHOW DATABASES privilege is missing|
|[MXS-39](https://mariadb.atlassian.net/browse/MXS-39)|bugzillaId-731:Boolean configuration parameters accept inconsistent parameters|
|[MXS-64](https://mariadb.atlassian.net/browse/MXS-64)|maxkeys and Maxpasswd do not produce informative error output|
|[MXS-25](https://mariadb.atlassian.net/browse/MXS-25)|bugzillaId-656: MySQL Monitor: claims that Master is available after master failure|
|[MXS-82](https://mariadb.atlassian.net/browse/MXS-82)|cmake warns when mariadb is compiled without mysql_release|
|[MXS-69](https://mariadb.atlassian.net/browse/MXS-69)|dbfwfilter should be pessimistic about rule syntax errors|
|[MXS-98](https://mariadb.atlassian.net/browse/MXS-98)|regexfilter log|
|[MXS-28](https://mariadb.atlassian.net/browse/MXS-28)|bugzillaId-433: Logging don't include assert information|
|[MXS-75](https://mariadb.atlassian.net/browse/MXS-75)|"wildcard" rule also blocks COUNT(*)|
|[MXS-118](https://mariadb.atlassian.net/browse/MXS-118)|Two monitors loaded at the same time result into not working installation|
|[MXS-33](https://mariadb.atlassian.net/browse/MXS-33)|bugzillaId-702: CLI: list services command shows negative values for the number of users of a service (Read Service).|
|[MXS-17](https://mariadb.atlassian.net/browse/MXS-17)|bugzillaId-736: Memory leak while doing read/write splitting|
|[MXS-30](https://mariadb.atlassian.net/browse/MXS-30)|bugzillaId-487: Buffer manager should not use pointer arithmetic on void*|

## Known Issues

There are a number bugs and known limitations within this version of MaxScale, the most serious of this are listed below.

* The Read/Write Splitter is a little too strict when it receives errors from slave servers during execution of session commands. This can result in sessions being terminated in situation in which MaxScale could recover without terminating the sessions.

* MaxScale can not manage authentication that uses wildcard matching in hostnames in the mysql.user table of the backend database. The only wildcards that can be used are in IP address entries.

* When users have different passwords based on the host from which they connect MaxScale is unable to determine which password it should use to connect to the backend database. This results in failed connections and unusable usernames in MaxScale.

* Binlog Router Plugin is compatible with MySQL 5.6  
  Binlog Router Plugin currently does not work for MariaDB 5.5 and MariaDB 10.0 

* LONGBLOB are currently not supported.

* Galera Cluster variables, such as @@wsrep_node_name, are not resolved by the embedded MariaDB parser.

* The Database Firewall filter does not support multi-statements. Using them will result in an error being sent to the client.   

## Packaging

Both RPM and Debian packages are available for MaxScale in addition to the tar based releases previously distributed we now provide

* CentOS/RedHat 5

* CentOS/RedHat 6

* CentOS/RedHat 7

* Debian 6

* Debian 7

* Ubuntu 12.04 LTS

* Ubuntu 14.04 LTS

* Fedora 19

* Fedora 20

* Fedora 21

* OpenSuSE 13

* SuSE Linux Enterprise 11

* SuSE Linux Enterprise 12
