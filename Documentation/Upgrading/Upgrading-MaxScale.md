# Upgrading MariaDB MaxScale

For more information about what has changed, please refer to the
[ChangeLog](../Changelog.md) and to the
[release notes](../Release-Notes/).

Before starting the upgrade, any existing configuration files should
be backed up.

[TOC]

# Upgrading MariaDB MaxScale from 24.02 to 24.08

## Readwritesplit

### `reuse_prepared_statements`

The `reuse_prepared_statements` parameter has been replaced with the use of the
[PsReuse](../Filters/PsReuse.md) filter module.

The functionality that previously was enabled with:

```
[My-Readwritesplit]
type=service
router=readwritesplit
reuse_prepared_statements=true
```

Should now be implemented with:

```
[PsReuse]
type=filter
module=psreuse

[My-Readwritesplit]
type=service
router=readwritesplit
filters=PsReuse
```

### `optimistic_trx`

The `optimistic_trx` parameter has been replaced with the use of the
[OptimisticTrx](../Filters/OptimisticTrx.md) filter module.

The functionality that previously was enabled with:

```
[My-Readwritesplit]
type=service
router=readwritesplit
optimistic_trx=true
```

Should now be implemented with:

```
[OptimisticTrx]
type=filter
module=optimistictrx

[My-Readwritesplit]
type=service
router=readwritesplit
transaction_replay=true
filters=OptimisticTrx
```

# Upgrading MariaDB MaxScale from 23.08 to 24.02

No specific actions needed.


# Upgrading MariaDB MaxScale from 23.02 to 23.08

No specific actions needed.


# Upgrading MariaDB MaxScale from 22.08 to 23.02

## Removed Features

* The `csmon` and `auroramon` monitors have been removed.

* The obsolete `maxctrl drain` command has been removed.

* The `maxctrl cluster` commands have been removed.


# Upgrading MariaDB MaxScale from 21.06 to 22.08

## Removed Features

* The support for legacy encryption keys generated with `maxkeys` from pre-2.5
  versions has been removed. This feature was deprecated in MaxScale 2.5 when
  the new key storage format was introduced. To migrate to the new key storage
  format, create a new key file with `maxkeys` and re-encrypt the passwords with
  `maxpasswd`.

* The deprecated Database Firewall filter has been removed.


# Upgrading MariaDB MaxScale from 2.5 to 21.06

**NOTE** MaxScale 6.4 was renamed to 21.06 in May 2024. Thus, what would have
been released as 6.4.16 in June, was released as 21.06.16. The purpose of this
change is to make the versioning scheme used by all MaxScale series
identical. 21.06 denotes the year and month when the first 6 release was made.

## Duration Type Parameters

Using duration type parameters without an explicit suffix has been deprecated in
MaxScale 2.4. In MaxScale 6 they are no longer allowed when used with the REST
API or MaxCtrl. This means that any `create` or `alter` commands in MaxCtrl that
use a duration type parameter must explicitly specify the suffix of the unit.

For example, the following command:

```
maxctrl alter service My-Service connection_keepalive 30000
```

should be replaced with:

```
maxctrl alter service My-Service connection_keepalive 30000ms
```

Duration type parameters can still be defined in the configuration file without
an explicit suffix but this behavior is deprecated. The recommended approach is
to add explicit suffixes to all duration type parameters when upgrading to
MaxScale 6.

## Changed Parameters

### `threads`

The default value of `threads` was changed to `auto`.

## Removed Parameters

### Core Parameters

The following deprecated core parameters have been removed:

- `thread_stack_size`

### Schemarouter

The deprecated aliases for the schemarouter parameters `ignore_databases` and
`ignore_databases_regex` have been removed. They can be replaced with
`ignore_tables` and `ignore_tables_regex`.

In addition, the `preferred_server` parameter that was deprecated in 2.5 has
also been removed.

### `mariadbmon`

* MariaDBMonitor settings `ignore_external_masters`, `detect_replication_lag`
  `detect_standalone_master`, `detect_stale_master` and `detect_stale_slave`
  have been removed. The first two were ineffective, the latter three are
  replaced by `master_conditions` and `slave_conditions`.

## Session Command History

The `prune_sescmd_history`, `max_sescmd_history` and `disable_sescmd_history`
have been made into generic service parameters that are shared between all
routers that support it.

The default value of `prune_sescmd_history` was changed from `false` to
`true`. This was done as most MaxScale installations either benefit from it
being enabled or are not affected by it.


# Upgrading MariaDB MaxScale from 2.4 to 2.5

## MaxAdmin

The deprecated MaxAdmin interface has been removed in 2.5.0 in favor of the REST
API and the MaxCtrl command line client. The `cli` and `maxscaled` modules can
no longer be used.

## Authentication

The credentials used by services now require additional grants. For a full list
of required grants, refer to the
[protocol documentation](../Authenticators/Authentication-Modules.md#required-grants).

## MariaDB-Monitor

The settings `detect_stale_master`, `detect_standalone_master` and
`detect_stale_slave`  are replaced by `master_conditions` and
`slave_conditions`. The old settings may still be used, but will be removed in
a later version.

### Password encryption

The encrypted passwords feature has been updated to be more secure. Users are
recommended to generate a new encryption key and re-encrypt their passwords
using the `maxkeys` and `maxpasswd` utilities. Old passwords still work.

## Default Server State

The default state of servers in 2.4 was `Running` and in 2.5 it is now
`Down`. This was done to prevent newly added servers from being accidentally
used before they were monitored.

## Columnstore Monitor

It is now mandatory to specify in the configuration what version the
monitored Columnstore cluster is.
```
[CSMonitor]
type=monitor
module=csmon
version=1.5
...
```
Please see the [documentation](../Monitors/ColumnStore-Monitor.md#master-selection)
for details.

## New binlog router

The binlog router delivered with MaxScale 2.5 is completely new and
not 100% backward compatible with the binlog router delivered with
earlier MaxScale versions. If you use the binlog router, carefully
assess whether the functionality provided by the new one fulfills
your requirements, before upgrading MaxScale.

## Tee Filter

The tee filter parameter `service` has been deprecated in favor of the `target`
parameter. All usages of `service` can be replaced with `target`.


# Upgrading MariaDB MaxScale from 2.3 to 2.4

## Section Names

### Reserved Names

Section and object names starting with `@@` are now reserved for
internal use by MaxScale.

In case such names have been used, they must manually be changed
in all configuration files of MaxScale, before MaxScale 2.4 is started.

Those files are:

* The main configuration file; typically `/etc/maxscale.cnf`.
* All nested configuration files; typically `/etc/maxscale.cnf.d/*`.
* All dynamic configuration files; typically `/var/lib/maxscale/maxscale.cnd.d/*`.

### Whitespace in Names

Whitespace in section names that was deprecated in MaxScale 2.2 will now be
rejected, which will cause the startup of MaxScale to fail.

To prevent that, section names like
```
[My Server]
...

[My Service]
...
servers=My Server
```
must be changed, for instance, to
```
[MyServer]
...

[MyService]
...
servers=MyServer
```

## Durations

Durations can now be specified using one of the suffixes `h`, `m`, `s`
and `ms` for specifying durations in hours, minutes, seconds and
milliseconds, respectively.

_Not_ providing an explicit unit has been deprecated in MaxScale 2.4,
so it is advisable to add suffixes to durations. For instance,
```
some_param=60s
some_param=60000ms
```

## Improved Admin User Encryption

MaxScale 2.4 will use a SHA2-512 hash for new admin user passwords. To upgrade a
user to use the better hashing algorithm, either recreate the user or use the
`maxctrl alter user` command.

## MariaDB-Monitor

The following settings have been removed and cause a startup error
if defined:

* `mysql51_replication`
* `multimaster`
* `allow_cluster_recovery`.

## ReadWriteSplit

* If multiple masters are available for a readwritesplit service, the one with
  the lowest connection count is selected.

* If a master server is placed into maintenance mode, all open transactions are
  allowed to gracefully finish before the session is closed. To forcefully close
  the connections, use the `--force` option for `maxctrl set server`.

* The `lazy_connect` feature can be used as a workaround to
  [MXS-619](https://jira.mariadb.org/browse/MXS-619). It also reduces the
  overall load on the system when connections are rapidly opened and closed.

* Transaction replays now have a limit on how many times a replay is
  attempted. The default values is five attempts and is controlled by the
  `transaction_replay_attempts` parameter.

* If transaction replay is enabled and a deadlock occurs (SQLSTATE 40XXX), the
  transaction is automatically retried.


# Upgrading MariaDB MaxScale from 2.2 to 2.3

## Increased Memory Use

Starting with MaxScale 2.3.0 up to 40% of the memory can be used for
caching parsed queries. The most noticeable change is that it improves
performance in almost all cases where queries need to be parsed. Most of
the time this happens when the readwritesplit router or filters are used.

The amount of memory that MaxScale uses can be controlled with the
`query_classifier_cache_size` parameter. For example, to limit the total
memory to 1GB, add `query_classifier_cache_size=1G` to your
configuration. To disable it, set the value to `0`.

In addition to the aforementioned query classifier caching, the
readwritesplit session command history is enabled by default in 2.3 but is
limited to a maximum of 50 commands after which the history is
disabled. This is unlikely to show in any metrics but it contributes to
the increased memory footprint of MaxScale.

## Unknown Global Parameters

All unknown parameters are now treated as errors. Check your configuration for
errors if MaxScale fails to start after upgrading to 2.3.1.

## `passwd` is deprecated

In the configuration file, passwords for monitors and services should be
specified using `password`; the support for the deprecated
`passwd` will be removed in the future. That is, the following
```
[The-Service]
type=service
passwd=some-service-password
...

[The-Monitor]
type=monitor
passwd=some-monitor-password
...
```
should be changed to
```
[The-Service]
type=service
password=some-service-password
...

[The-Monitor]
type=monitor
password=some-monitor-password
...
```

## `authenticator_options` for servers is ignored

Authenticator options are now only used with listeners.


# Upgrading MariaDB MaxScale from 2.1 to 2.2

### Administrative Users

The file format for the administrative users used by MaxScale has been
changed. Old style files are automatically upgraded and a backup of the old file is
stored in `/var/lib/maxscale/passwd.backup`.

### Regular Expression Parameters

Modules may now use a built-in regular expression string parameter type instead
of a normal string when accepting patterns. The modules that use the new regex
parameter type are *qlafilter* and *tee*. When inputting pattern, enclose the
string in slashes, e.g. `match=/^select/` defines the pattern `^select`.

### Binlog Server

Binlog server automatically accepts GTID connection from MariaDB 10 slave servers
by saving all incoming GTIDs into a SQLite map database.

### MaxCtrl Included in Main Package

In the 2.2.1 beta version MaxCtrl was in its own package whereas in 2.2.2
it is in the main `maxscale` package. If you have a previous installation
of MaxCtrl, please remove it before upgrading to MaxScale 2.2.2.


# Upgrading MariaDB MaxScale from 2.0 to 2.1

## IPv6 Support

MaxScale 2.1.2 added support for IPv6 addresses. The default interface that listeners bind to
was changed from the IPv4 address `0.0.0.0` to the IPv6 address `::`. To bind to the old IPv4 address,
add `address=0.0.0.0` to the listener definition.

## Persisted Configuration Files

Starting with MaxScale 2.1, any changes made with the newly added
[runtime configuration change](../Reference/MaxAdmin.md#runtime-configuration-changes)
will be persisted in a configuration file. These files are located in `/var/lib/maxscale/maxscale.cnf.d/`.

## MaxScale Log Files

The name of the log file was changed from _maxscaleN.log_ to _maxscale.log_. The
default location for the log file is _/var/log/maxscale/maxscale.log_.

Rotating the log files will cause MaxScale to reopen the file instead of
renaming them. This makes the MaxScale logging facility _logrotate_ compatible.

## ReadWriteSplit

The `disable_sescmd_history` option is now enabled by default. This means that
slaves will not be recovered mid-session even if a replacement slave is
available. To enable the legacy behavior, add the `disable_sescmd_history=true`
parameter to the service definition.

## Persistent Connections

The MariaDB session state is reset in MaxScale 2.1 for persistent
connections. This means that any modifications to the session state (default
database, user variable etc.) will not survive if the connection is put into the
connection pool. For most users, this is the expected behavior.

## User Data Cache

The location of the MariaDB user data cache was moved from
`/var/cache/maxscale/<Service>` to `/var/cache/maxscale/<Service>/<Listener>`.

## Galeramon Monitoring Algorithm

Galeramon will assign the master status *only* to the node which has a
_wsrep_local_index_ value of 0. This will guarantee consistent writes with
multiple MaxScales but it also causes slower changes of the master node.

To enable the legacy behavior, add `root_node_as_master=false` to the Galera
monitor configuration.

## MaxAdmin Editing Mode

The default editing mode was changed from _vim_ to _emacs_ mode. To start
maxadmin in the legacy mode, use the `-i` option.


# Upgrading MariaDB MaxScale from 1.4 to 2.0

## MaxAdmin

The default way the communication between MaxAdmin and MariaDB MaxScale is
handled has been changed from an internet socket to a Unix domain socket.
The former alternative is still available but has been _deprecated_.

If no arguments are given to MaxAdmin, it will attempt to connect to
MariaDB MaxScale using a Unix domain socket. After the upgrade you will
need to provide at least one internet socket related flag - `-h`, `-P`,
`-u` or `-p` - to force MaxAdmin to use the internet socket approach.

E.g.

    user@host $ maxadmin -u admin

## MySQL Monitor

The MySQL Monitor now assigns the stale state to the master server by default.
In addition to this, the slave servers receive the stale slave state when they
lose the connection to the master. This should not cause changes in behavior
but the output of MaxAdmin will show new states when replication is broken.


# Upgrading MaxScale from 1.3 to 1.4

## Service user permissions

The service users now also need SELECT privileges on mysql.tables_priv. This is
required for the resolution of table level grants. To grant SELECT privileges
for the service user, replace the user and hostname in the following example.

```
GRANT SELECT ON mysql.tables_priv TO 'username'@'maxscalehost';
```

## Password encryption

MaxScale 1.4 upgrades the used password encryption algorithms to more secure ones.
This requires that the password files are recreated with the `maxkeys` tool.
For more information about how to do this, please read the installation guide:
[MariaDB MaxScale Installation Guide](../Getting-Started/MariaDB-MaxScale-Installation-Guide.md)

## SSL

The SSL configuration parameters are now a part of the listeners. If a service
used the old style SSL configuration parameters, the values should be moved to
the listener which is associated with that service.

Here is an example of an old style configuration.

```
[RW-Split-Router]
type=service
router=readwritesplit
servers=server1,server2,server3,server4
user=jdoe
passwd=BD26E4139A15280CA882264AA1551C70
ssl=required
ssl_cert=/home/user/certs/server-cert.pem
ssl_key=/home/user/certs/server-key.pem
ssl_ca_cert=/home/user/certs/ca.pem
ssl_version=TLSv12

[RW-Split-Listener]
type=listener
service=RW-Split-Router
protocol=MySQLClient
port=3306
```

And here is the new, 1.4 compatible configuration style.

```
[RW-Split-Router]
type=service
router=readwritesplit
servers=server1,server2,server3,server4
user=jdoe
passwd=BD26E4139A15280CA882264AA1551C70

[RW-Split-Listener]
type=listener
service=RW-Split-Router
protocol=MySQLClient
port=3306
ssl=required
ssl_cert=/home/user/certs/server-cert.pem
ssl_key=/home/user/certs/server-key.pem
ssl_ca_cert=/home/user/certs/ca.pem
ssl_version=TLSv12
```

Please also note that the `enabled` SSL mode is no longer supported due to
the inherent security issues with allowing SSL and non-SSL connections on
the same port. In addition to this, SSLv3 is no longer supported due to
vulnerabilities found in it.


# Upgrading MaxScale from 1.2 to 1.3

## Binlog Router

The master server details are now provided with a **master.ini** file located in
the binlog directory and it can be changed using a CHANGE MASTER TO command issued
via a MySQL connection to MaxScale.

This file, properly filled, is now mandatory and without it the binlog router
cannot connect to the master database.

Before starting binlog router after MaxScale 1.3 upgrade, please add relevant
information to *master.ini*, example:

```
[binlog_configuration]
master_host=127.0.0.1
master_port=3308
master_user=repl
master_password=somepass
filestem=repl-bin
```

Additionally, the option ```servers=masterdb``` in the service definition is no
longer required.


# Upgrading MaxScale from 1.1 to 1.2

This document describes upgrading MaxScale from version 1.1.1 to 1.2 and
the major differences in the new version compared to the old version. The
major changes can be found in the `Changelog.txt` file in the installation
directory and the official release notes in the `ReleaseNotes.txt` file.

## Installation

Upgrading MaxScale will copy the `MaxScale.cnf` file in
`/usr/local/mariadb-maxscale/etc/` to `/etc/` and renamed to `maxscale.cnf`.
Binary log files are not automatically copied and should be manually moved
from `/usr/local/mariadb-maxscale` to `/var/lib/maxscale/`.

## File location changes

MaxScale 1.2 follows the [FHS-standard](http://www.pathname.com/fhs/) and
installs to `/usr/` and `/var/` subfolders. Here are the major changes and
file locations.

* Configuration files are located in `/etc/` and use lowercase letters: `/etc/maxscale.cnf`
* Binary files are in `/usr/bin/`
* Libraries and modules are in `/usr/lib64/maxscale/`. If you are using custom modules, please make sure they are in this directory before starting MaxScale.
* Log files are in the `var/log/maxscale/` folder
* MaxScale's PID file is located in `/var/run/maxscale/maxscale.pid`
* Data files and other persistent files are in `/var/lib/maxscale/`

## Running MaxScale without root permissions

MaxScale can run as a non-root user with the 1.2 version. RPM and DEB
packages install the `maxscale` user and `maxscale` group which are used
by the init scripts and systemd configuration files. If you are installing
from a binary tarball, you can run the `postinst` script included in it to
manually create these groups.


# Upgrading MaxScale from 1.0 to 1.1

This document describes upgrading MaxScale from version 1.0.5 to 1.1.0 and
the major differences in the new version compared to the old version. The
major changes can be found in the `Changelog.txt` file in the installation
directory and the official release notes in the `ReleaseNotes.txt` file.

## Installation

If you are installing MaxScale from a RPM package, we recommend you back
up your configuration and log files and that you remove the old installation
of MaxScale completely. If you choose to upgrade MaxScale instead of removing
it and re-installing it afterwards, the init scripts in `/etc/init.d` folder
will be missing. This is due to the RPM packaging system but the script can
be re-installed by running the `postinst` script found in the
`/usr/local/mariadb-maxscale` folder.

```
# Re-install init scripts
cd /usr/local/mariadb-maxscale
./postinst
```

The 1.1.0 version of MaxScale installs into `/usr/local/mariadb-maxscale`
instead of `/usr/local/skysql/maxscale`. This will cause external references
to MaxScale's home directory to stop working so remember to update all
paths with the new version.

## MaxAdmin changes

The MaxAdmin client's default password in MaxScale 1.1.0 is `mariadb`
instead of `skysql`.
