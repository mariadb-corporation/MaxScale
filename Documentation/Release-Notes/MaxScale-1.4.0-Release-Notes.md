
# MariaDB MaxScale 1.4.0 (Beta) Release Notes

Release 1.4.0 is a beta release.

This document describes the changes in release 1.4.0, when compared to
release 1.3.0.

## 1.4.0

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## New Features

### Firewall Filter

The firewall filter now supports different actions when a rule is matched.
Currently possible actions are to either block the query, allow it or
ignore the match and allow it.

Matching and non-matching queries can now be logged and by combining this new
logging functionality with the _ignore_ action, you can set up the filter in
a dry-run mode. For more information about the firewall filter, please refer to
[Database Firewall Filter](../Filters/Database-Firewall-Filter.md).

### SSL

Client-side SSL support has been in MaxScale for some time, but has
been known to have been unstable. In 1.4.0, client side SSL is now
believed to be stable and fully usable.

The SSL configuration is now done on a per listener basis which
allows both SSL and non-SSL connections to a service. For more details
on how to configure this, please refer to the
[MaxScale Configuration Guide](../Getting-Started/Configuration-Guide.md#listener-and-ssl).

### POSIX Extended Regular Expression Syntax

The _qlafilter_, the _topfilter_ and the _namedserverfilter_ now
accept _extended_ as a filter option, which enables the POSIX Extended
Regular Expression syntax.


### Improved user grant detection

MaxScale now allows users with only table level access to connect with
a default database. The service users will require SELECT privileges on
the `mysql.tables_priv` table:

```
GRANT SELECT ON mysql.tables_priv TO 'maxscale'@'maxscalehost'
```
For more information, refer to the configuration guide:
[MaxScale Configuration Guide](../Getting-Started/Configuration-Guide.md#service).

### Improved password encryption

MaxScale 1.4.0 uses the MD5 version of the crypt function which is more secure
than the non-MD5 version. This means that a new password file needs to be
created with `maxkeys`. The configuration file should be updated to use the new
passwords. This can be done with the help of the `maxpasswd` utility. For more
details about how to do this, please refer to the installation guide:
[MariaDB MaxScale Installation Guide](../Getting-Started/MariaDB-MaxScale-Installation-Guide.md)

## Removed Features

* MaxScale no longer supports SSLv3.

* The `enabled` mode, which allows both SSL and non-SSL connections on the same port, has been removed.

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 1.3.0.](https://jira.mariadb.org/browse/MXS-600?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20resolution%20in%20(Fixed%2C%20Done)%20AND%20fixVersion%20%3D%201.4.0)

 * [MXS-400](https://jira.mariadb.org/browse/MXS-400): readwritesplit router doesn't allow connect when the only remaining server is master and slave
 * [MXS-497](https://jira.mariadb.org/browse/MXS-497): MaxScale does not contemplate client multiple statements (CLIENT_MULTI_STATEMENTS)
 * [MXS-504](https://jira.mariadb.org/browse/MXS-504): SSL connection handling needs work
 * [MXS-511](https://jira.mariadb.org/browse/MXS-511): ReadWriteSplit router won't choose node as master and logs confusing "RUNNING MASTER" error message
 * [MXS-563](https://jira.mariadb.org/browse/MXS-563): Maxscale fails to start
 * [MXS-565](https://jira.mariadb.org/browse/MXS-565): Binlog Router doesn't handle 16MB larger transmissions
 * [MXS-573](https://jira.mariadb.org/browse/MXS-573): Write permission to systemd service file
 * [MXS-574](https://jira.mariadb.org/browse/MXS-574): Wrong parameter name in systemd service file
 * [MXS-575](https://jira.mariadb.org/browse/MXS-575): Nagios scripts lack execute permissions
 * [MXS-577](https://jira.mariadb.org/browse/MXS-577): Don't install systemd files and init.d scipts at the same time
 * [MXS-581](https://jira.mariadb.org/browse/MXS-581): Only the first 8 characters of passwords are used
 * [MXS-582](https://jira.mariadb.org/browse/MXS-582): crypt is not thread safe
 * [MXS-585](https://jira.mariadb.org/browse/MXS-585): Intermittent connection failure with MaxScale 1.2/1.3 using MariaDB/J 1.3
 * [MXS-589](https://jira.mariadb.org/browse/MXS-589): Password encryption looks for the file in the wrong directory
 * [MXS-592](https://jira.mariadb.org/browse/MXS-592): Build failure with MariaDB 10.1 when doing a debug build
 * [MXS-594](https://jira.mariadb.org/browse/MXS-594): Binlog name gets trunkated
 * [MXS-600](https://jira.mariadb.org/browse/MXS-600): Threads=auto parameter configuration fails

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
