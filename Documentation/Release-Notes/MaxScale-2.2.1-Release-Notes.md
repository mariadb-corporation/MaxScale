# MariaDB MaxScale 2.2.1 Release Notes -- 2018-01-11

Release 2.2.1 is a Beta release.

This document describes the changes in release 2.2.1, when compared to
release 2.2.0.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Process identity

By default, MaxScale can no longer be run as `root`, but must be run as some
other user. However, it is possible to start MaxScale as `root`, as long as
the user to run MaxScale as is provided as a command line argument:
```
root@host:~# maxscale --user=maxuser ...
```
If it is imperative to run MaxScale as root, e.g. in a Docker container, it
can be achieved by invoking MaxScale as root and by explicitly specifying
the user to also be root:
```
root@host:~# maxscale --user=root ...
```

### Binlog server

* The `mariadb10_slave_gtid` parameter was removed and slave connections can now
  always register with MariaDB 10 GTID.
  This means the gtid_maps SQLite database is always updated.

* The `binlog_structure` parameter was removed and the binlogs are stored
  automatically in 'tree' mode when `mariadb10_master_gtid` is enabled
  (GTID registration to master).

* If `mariadb10_master_gtid` is enabled, the `transaction_safety` is
  automatically enabled. In MaxScale 2.2.0, if `transaction_safety` was disabled
  when `mariadb10_master_gtid` was enabled MaxScale would refuse to start.

* The binlogrouter can accept GTID slave registration from MariaDB 10.X slaves
  and can also register to Master server MariaDB 10.x using GTID.

### Module names and case sensitivity

* The filenames of all modules have been made lowercase.
* When specifying a module, the name matching is case insensitive.

In practice this means that in the configuration file, the following
are all equivalent:
```
router=readwritesplit

router=READWRITESPLIT

router=ReadWriteSplit
```

### MySQL/MariaDB Client Protocol

The shared object implementing the client protocol has been renamed
from `libMySQLClient.so` to `libmaridbclient.so`. In practice this means
that, in the MaxScale configuration file, `MySQLClient` should be replaced
with `mariadbclient` or, e.g., `MariaDBClient`, as module names are matched
in a case insensitive manner.

As an example, a listener section like
```
[TheListener]
type=listener
...
protocol=MySQLClient
```
should be changed into
```
[TheListener]
type=listener
...
protocol=MariaDBClient
```
*NOTE* Using `MySQLClient` is still supported, but has been deprecated.

### MySQL Backend Protocol

The shared object implementing the backend protocol has been renamed
from `libMySQLBackend.so` to `libmaridbbackend.so`. In practice this means
that, in the MaxScale configuration file, `MySQLBackend` should be replaced
with `mariadbbackend` or, e.g., `MariaDBBackend`, as module names are matched
in a case insensitive manner.

As an example, a server section like
```
[TheServer]
type=server
...
protocol=MySQLBackend
```
should be changed into
```
[TheServer]
type=server
...
protocol=MariaDBBackend
```
*NOTE* Using `MySQLBackend` is still supported, but has been deprecated.

### MySQL Monitor

Renamed to [MariaDB Monitor](../Monitors/MariaDB-Monitor.md).

Note that this affects the module name as well. Up until MaxScale 2.2.0
a configuration section referring to this monitor would look like
```
[MyMonitor]
type=monitor
module=mysqlmon
...
```
but from MaxScale 2.2.1 onwards it should look like
```
[MyMonitor]
type=monitor
module=mariadbmon
...
```
The name `mysqlmon` has been deprecated but can still be used, although it will
cause a warning to be logged.

### MariaDB Monitor

The default value of the configuration parameter `detect_standalone_master` has
been changed from `false` to `true`.

### ReadWritesplit

The default value of `strict_multi_stmt` was changed to `false` to make
usage of atomic compound statements and multi-statement queries less
restrictive and to align it with the default value of `strict_sp_calls`.

Most cases where the functionality of `strict_multi_stmt` was triggered
were cases where the added safety of locking a session to the master did
more harm than it did good.

The only case where `strict_multi_stmt` should be enabled is when a
multi-statement or a compound statement modifies the state of the
session. This is not a good practice and a change in the client side
behavior is advised.

### Top Filter

The top filter now uses the session ID instead of an internal counter for the
names of the log file names.

### MaxCtrl

The `-h, --hosts` argument was changed to accept a list of hostnames separated
by commas instead of spaces. This prevents commands from accidentally being
interpreted as hostnames.

## Dropped Features

## New Features

### MariaDB Monitor

MariaDB Monitor can now perform *failover* (replace a dead master), *switchover*
(replace a running master) and *rejoin* (join a standalone node to the
master-slave cluster). All of these features only work with a simple 1-master
N-slaves cluster using Gtid replication. Failover and switchover can be
activated through maxadmin or the REST-API. Failover and rejoin can be set to
activate automatically. For more information, see the
[MariaDB Monitor documentation](../Monitors/MariaDB-Monitor.md).

### REST API Relationship Endpoints

The _servers_, _monitors_ and _services_ types now support direct updating of
relationships via the `relationships` endpoints. This conforms to the JSON API
specification on updating resource relationships.

For more information, refer to the REST API documentation. An example of this
can be found in the
[Server Resource documentation](../REST-API/Resources-Server.md#update-server-relationships).

### PL/SQL Comaptibility

The parser of MaxScale has been extended to support the PL/SQL compatibility
features of the upcoming 10.3 release. For more information on how to enable
this mode, please refer to the
[configuration guide](../Getting-Started/Configuration-Guide.md#sql_mode).

This functionality was available already in MaxScale 2.2.0.

### Environment Variables in the configuration file

If the global configuration entry `substitute_variables` is set to true,
then if the first character of a value in the configuration file is a `$`
then everything following that is interpreted as an environment variable
and the configuration value is replaced with the value of the environment
variable. For more information please consult the
[Configuration Guide](../Getting-Started/Configuration-Guide.md).

### Cache behaviour in transactions

It can now be specified how the cache should be behave when a transaction
is active. Please refer to the [documentation](../Filters/Cache.md)
for details.

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.2.1.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.2.1)

* [MXS-1593](https://jira.mariadb.org/browse/MXS-1593) Servers appearing as Stale out of list servers for a fresh setup
* [MXS-1588](https://jira.mariadb.org/browse/MXS-1588) Switchover fails randomly
* [MXS-1582](https://jira.mariadb.org/browse/MXS-1582) Maxscale leaving socket behind after shutdown
* [MXS-1545](https://jira.mariadb.org/browse/MXS-1545) Fix GTID connecting slave error detections
* [MXS-1541](https://jira.mariadb.org/browse/MXS-1541) Top filter uses internal ID instead of session ID for file suffix
* [MXS-1527](https://jira.mariadb.org/browse/MXS-1527) SELECT with session var is not supported
* [MXS-1525](https://jira.mariadb.org/browse/MXS-1525) Firewall filter does not check exact match for host
* [MXS-1519](https://jira.mariadb.org/browse/MXS-1519) Firewall instances can interfere with each other
* [MXS-1517](https://jira.mariadb.org/browse/MXS-1517) Retain stale master status even if the master goes down
* [MXS-1499](https://jira.mariadb.org/browse/MXS-1499) Add missing fields to SHOW ALL SLAVES STATUS
* [MXS-1486](https://jira.mariadb.org/browse/MXS-1486) The cache does not always update the cached entry even if it could
* [MXS-1461](https://jira.mariadb.org/browse/MXS-1461) NOT operation needed for firewall rule
* [MXS-1408](https://jira.mariadb.org/browse/MXS-1408) maxadmin not working in latest version
* [MXS-1327](https://jira.mariadb.org/browse/MXS-1327) set   log-priority by  maxadmin

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
