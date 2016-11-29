# MariaDB MaxScale 2.1.0 Release Notes

Release 2.1.0 is a Beta release.

This document describes the changes in release 2.1.0, when compared to
release 2.0.X.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Configuration Files

From 2.1.0 onwards MariaDB MaxScale supports hierarchical configuration
files. When invoked with a configuration file, e.g. `maxscale.cnf`, MariaDB
MaxScale looks for a directory `maxscale.cnf.d` in the same directory as the
configuration file, and reads all `.cnf` files it finds in that directory
hierarchy. All other files will be ignored.

Please see the
[Configuration Guide](../Getting-Started/Configuration-Guide.md#configuration)
for details.

### Logging

Before version 2.1.0, MaxScale created in the log directory a log file
maxscaleN.log, where N initially was 1 and then was increased every time
MaxScale was instructed (by sending the signal SIGUSR1 or via maxadmin)
to rotate the log file.

That has now been changed so that the name of the log file is *always*
maxscale.log and when MaxScale is instructed to rotate the log file,
MaxScale simply closes it and then reopens and truncates it.

To retain the existing log entries, you should first move the file to
another name (MaxScale continues writing to it) and then instruct
MaxScale to rotate the the log file.

```
    $ mv maxscale.log maxscale1.log
    $ # MaxScale continues to write to maxscale1.log
    $ kill -SIGUSR1 <maxscale-pid>
    $ # MaxScale closes the file (i.e. maxscale1.log) and reopens maxscale.log
```

This behaviour is now compatible with logrotate(8).

Further, if MaxScale is configured to use shared memory for the log file,
the file is created into the directory "/dev/shm/maxscale". Earlier the
log file was created into the directory "/dev/shm/maxscale.PID", where PID
was the pid of the MaxScale process.

In addition, there is now a mechanism that prevents the flooding of the log, in
case the same error occurs over and over again. That mechanism, which is enabled
by default, is configured using the new global configuration entry `log_throttling`.
For more information about this configuration entry, please see
[Global Settings](../Getting-Started/Configuration-Guide.md#global-settings).

### Persistent Connections

Starting with the 2.1 version of MariaDB MaxScale, when a MySQL protocol
persistent connection is taken from the persistent connection pool, the
state of the MySQL session will be reset when the the connection is used
for the first time. This allows persistent connections to be used with no
functional limitations and makes them behave like normal MySQL
connections.

For more information about persistent connections, please read the
[Administration Tutorial](../Tutorials/Administration-Tutorial.md).

### User data cache

The user data cache stores the cached credentials that are used by some router
modules. In 2.1.0, the authenticator modules are responsible for the persisting
of the user data cache. Currently, only the MySQLAuth module implements user
data caching.

The user data loaded from the backend databases is now stored on a per listener
basis instead of a per service basis. In earlier versions, each service had its own
cache directory in `/var/cache/maxscale`. This directory contains cached user
data which is used there is no connectivity to the backend cluster.

In 2.1.0, each listener has its own sub-directory in the service cache
directory. The old caches in `/var/cache/maxscale` will need to be manually
removed if they are no longer used by older versions of MaxScale.

## New Features

### Dynamic server configuration

MaxScale can now change the servers of a service or a monitor at run-time. New
servers can also be created and they will persisted even after a restart. The
following new commands were added to maxadmin, see output of `maxadmin help
<command>` for more details.

- `create server`: Creates a new server
- `destroy server`: Destroys a created server
- `add server`: Adds a server to a service or a monitor
- `remove server`: Removes a server from a service or a monitor
- `alter server`: Alter server configuration
- `alter monitor`: Alter monitor configuration

With these new features, you can start MaxScale without the servers and define
them later.

# Module commands

## Module commands

Introduced in MaxScale 2.1, the module commands are special, module-specific
commands. They allow the modules to expand beyound the capabilities of the
module API. Currently, only MaxAdmin implements an interface to the module
commands.

All registered module commands can be shown with `maxadmin list functions` and
they can be executed with `maxadmin call function <domain> <name> ARGS...` where
_<domain>_ is the domain where the module registered the function and _<name>_
is the name of the function. _ARGS_ is a function specific list of arguments.

Read [Module Commands](../Reference/Module-Commands.md) documentation for more details.

In the 2.1 release of MaxScale, the [_dbfwfilter_}(../Filters/Database-Firewall-Filter.md)
and [_avrorouter_](../Routers/Avrorouter.md) implement module commands.

### Amazon RDS Aurora monitor

The new [Aurora Monitor](../Monitors/Aurora-Monitor.md) module allows monitoring
of Aurora clusters. The monitor detects which of the nodes are read replicas and
which of them is the real write node and assigns the appropriate status for each
node. This module also supports launchable scripts on monitored events. Read the
[Monitor Common Documentation](../Monitors/Monitor-Common.md) for more details.

### Multi-master mode for MySQL Monitor

The MySQL monitor now detects complex multi-master replication
topologies. This allows the mysqlmon module to be used as a replacement
for the mmmon module. For more details, please read the
[MySQL Monitor Documentation](../Monitors/MySQL-Monitor.md).

### Failover mode for MySQL Monitor

A simple failover mode has been added to the MySQL Monitor. This mode is
aimed for two node master-slave clusters where the slave can act as a
master in case the original master fails. For more details, please read
the [MySQL Monitor Documentation](../Monitors/MySQL-Monitor.md).

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 2.0.X.](https://jira.mariadb.org/browse/MXS-739?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20resolution%20in%20(Fixed%2C%20Done)%20AND%20fixVersion%20%3D%202.0.0)


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
