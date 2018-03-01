# MariaDB MaxScale 2.1.0 Release Notes -- 2017-02-16

Release 2.1.0 is a Beta release.

This document describes the changes in release 2.1.0, when compared to
release 2.0.4.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## License

The license of MaxScale has been changed from MariaDB BSL 1.0 to MariaDB BSL 1.1.

For more information about MariaDB BSL 1.1, please refer to
[MariaDB BSL11](https://www.mariadb.com/bsl11).

## Changed Features

### `router_options` to Parameters

The `router_options` values can also be given as parameters to the service for
the _readwritesplit_, _schemarouter_ and _binlogrouter_ modules.

What this means is that in MaxScale 2.1 the following _readwritesplit_
configration.

```
[RW Split Router]
type=service
router=readwritesplit
servers=server1
user=maxuser
passwd=maxpwd
router_options=slave_selection_criteria=LEAST_ROUTER_CONNECTIONS,max_sescmd_history=10,disable_sescmd_history=false
```

Can also be written in the following form.

```
[RW Split Router]
type=service
router=readwritesplit
servers=server1
user=maxuser
passwd=maxpwd
slave_selection_criteria=LEAST_ROUTER_CONNECTIONS
max_sescmd_history=10
disable_sescmd_history=false
```

### Configuration Files

From 2.1.0 onwards MariaDB MaxScale supports hierarchical configuration
files. When invoked with a configuration file, e.g. `maxscale.cnf`, MariaDB
MaxScale looks for a directory `maxscale.cnf.d` in the same directory as the
configuration file, and reads all `.cnf` files it finds in that directory
hierarchy. All other files will be ignored.

Please see the
[Configuration Guide](../Getting-Started/Configuration-Guide.md#configuration)
for details.

### Readwritesplit `disable_sescmd_history` option

The default value for `disable_sescmd_history` is now true. This new default
value will prevent the excessive memory use of long-lived connections. In
addition to this, it was not optimal to enable this option while the default
value for `max_slave_connections` was 100%, effectively making it useless.

### Module configurations

MaxScale 2.1 introduces a new directory for module configurations. This new
directory can be used to store module specific configuration files.

Any configuration parameter that accepts a path will also support relative
paths. If a relative path is given, the path is interpreted relative to
the module configuration directory. The default value is
_/etc/maxscale.modules.d_.

For example, the `dbfwfilter` rule files could be stored in
_/etc/maxscale.modules.d/my_rules.txt_ and referred to with
`rules=my_rules.txt`.

For more details, refer to the documentation of _module_configdir_ in the
[Configuration Guide](../Getting-Started/Configuration-Guide.md)

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
the file is created into the directory `/dev/shm/maxscale`. Earlier the
log file was created into the directory `/dev/shm/maxscale.PID`, where PID
was the pid of the MaxScale process.

In addition, there is now a mechanism that prevents the flooding of the log, in
case the same error occurs over and over again. That mechanism, which is enabled
by default, is configured using the new global configuration entry `log_throttling`.
For more information about this configuration entry, please see
[Global Settings](../Getting-Started/Configuration-Guide.md#global-settings).

### Readwritesplit Read Retry

In 2.1, Readwritesplit will retry failed SELECT statements that are
executed outside of transaction and with autocommit enabled. This allows
seamless slave failover and makes it transparent to the client.

Read the [Readwritesplit documentation](../Routers/ReadWriteSplit.md) on
`retry_failed_reads` for more details.

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

### Galeramon Monitoring Algorithm

The galeramon monitor will only choose nodes with a _wsrep_local_index_
value of 0 as the master. This allows multiple MaxScales to always choose
the same node as the write master node for the cluster. The old behavior
can be taken into use by disabling the new `root_node_as_master` option.

For more details, read the [Galeramon documentation](../Monitors/Galera-Monitor.md).

### MaxAdmin editing mode

MaxAdmin now defaults to Emacs editing mode instead of VIM. To activate
with VIM-mode start MaxAdmin with option -i.

### Named Server Filter
The source option can now handle wildcards such as:
192.168.%.%

For more details, read the [Named Server Filter documentation](../Filters/Named-Server-Filter.md).

## New Features

### Dynamic configuration

MaxScale 2.1 supports dynamic configuration of servers, monitors and
listeners. A set of new commands were added to maxadmin. See output of
`maxadmin help` and `maxadmin help { create | destroy | alter | add | remove }`
for more details.

#### Dynamic server configuration

MaxScale can now change the servers of a service or a monitor at run-time. New
servers can also be created and they will persisted even after a restart.

- `create server`: Creates a new server
- `destroy server`: Destroys a created server
- `add server`: Adds a server to a service or a monitor
- `remove server`: Removes a server from a service or a monitor
- `alter server`: Alter server configuration
- `alter monitor`: Alter monitor configuration

With these new features, you can start MaxScale without the servers and define
them later.

#### Dynamic listener configuration

New listeners for services can be created and destroyed at runtime. This allows
the services to adapt to changes in client traffic.

- `create listener`: Create a new listener
- `destroy listener`: Destroy a created listener. The listener will stop
  handling client requests and will be removed after the next restart of
  MaxScale.

In addition to these commands, individual listeners can now be stopped and started.

- `shutdown listener`: Stop a listener
- `restart listener`: Restart a listener

#### Dynamic monitor configuration

New monitors can be created, modified and destroyed at runtime. This allows new
clusters to be added into MaxScale by defining new monitors for them. The
monitor parameters can also be changed at runtime making them more adaptive and
allowing runtime tuning of parameters.

- `create monitor`: Create a new monitor
- `destroy monitor`: Destroy a created monitor
- `alter monitor`: Alter monitor parameters

### Module commands

Introduced in MaxScale 2.1, the module commands are special, module-specific
commands. They allow the modules to expand beyound the capabilities of the
module API. Currently, only MaxAdmin implements an interface to the module
commands.

All registered module commands can be shown with `maxadmin list commands` and
they can be executed with `maxadmin call command <module> <name> ARGS...` where
_<module>_ is the name of the module and _<name>_ is the name of the
command. _ARGS_ is a command specific list of arguments.

Read [Module Commands](../Reference/Module-Commands.md) documentation for more details.

In the 2.1 release of MaxScale, the [_dbfwfilter_}(../Filters/Database-Firewall-Filter.md),
[_avrorouter_](../Routers/Avrorouter.md), [_cache_](../Filters/Cache.md) and
[_masking_](../Filters/Masking.md) modules implement module commands.

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

### Permissive authentication mode for MySQLAuth

The MySQL authentication module supports the `skip_authentication` option which
allows authentication to always succedd in MaxScale. This option offloads the
actual authentication to the backend server and it can be used to implement a
secure version of a wildcard user.

### Consistent Critical Reads

MaxScale 2.1 comes with a new filter module, _ccrfilter_, which allows critical
reads to be routed to master after inserts. This will make reads after inserts
consistent while still allowing read scaling.

For more information, refer to the [CCRFilter](../Filters/CCRFilter.md)
documentation.

### Database Cache

A new filter module, _cache_, allows MaxScale to cache the results of SELECT
statements. This improves the performance of read-heavy workloads by reducing
the work the backend databases have to perform.

For more information, refer to the [Cache](../Filters/Cache.md) documentation.

### Result set masking

The new _masking_ filter can mask sensitive information from result sets. This
is commonly done to hide sensitive information while still allowing the database
to efficiently process the actual data.

For more information, refer to the [Masking](../Filters/Masking.md)
documentation.

### Result set limiting

The newly added _maxrows_ filter can restrict the maximum size of a returned
result set. This can be used to reduce the negative effects of unexpectedly
large result sets. It can also be used to improve security by preventing access
to large sets of data with a single query.

For more information, refer to the [Maxrows](../Filters/Maxrows.md)
documentation.

### Insert stream filter

The _insertstream_ filter converts bulk inserts into CSV data streams that are
consumed by the backend server via the LOAD DATA LOCAL INFILE mechanism. This
leverages the speed advantage of LOAD DATA LOCAL INFILE over regular inserts
while also reducing the overall network traffic by condensing the inserted
values into CSV.

For more information, refer to the [Insert Stream Filter](../Filters/Insert-Stream-Filter.md)
documentation.

### Galeramon Monitor new option
The `set_donor_nodes` option allows the setting of _global variable_ _wsrep_sst_donor_  with a list the preferred donor nodes (among slave ones).

For more details, read the [Galeramon documentation](../Monitors/Galera-Monitor.md).

### Binlog Server encrypted binlogs
The binlog server can optionally encrypt the events received from the master server: the setup requires MariaDB 10.1 master (with Encryption active) and the `mariadb10-compatibility=1` option set.

For more details, read the [Binlogrouter documentation](../Routers/Binlogrouter.md).

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 2.0.4.](https://jira.mariadb.org/browse/MXS-951?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20resolution%20in%20(Fixed%2C%20Done)%20AND%20fixVersion%20%3D%202.1.0%20AND%20fixVersion%20!%3D%202.0.1%20AND%20fixVersion%20!%3D%202.0.2%20AND%20fixVersion%20!%3D%202.0.3%20AND%20fixVersion%20!%3D%202.0.4)

* [MXS-1025](https://jira.mariadb.org/browse/MXS-1025) qc_sqlite always reports " Statement was parsed, but not classified"
* [MXS-977](https://jira.mariadb.org/browse/MXS-977) MaxAdmin show monitor output missing formatting
* [MXS-951](https://jira.mariadb.org/browse/MXS-951) Using utf8mb4 on galera hosts stops maxscale connections
* [MXS-889](https://jira.mariadb.org/browse/MXS-889) "Ungrade Test" Jenkins job fails with CeentOS/RHEL 5 and SLES11
* [MXS-887](https://jira.mariadb.org/browse/MXS-887) create_env Jenkin job fails
* [MXS-873](https://jira.mariadb.org/browse/MXS-873) Changing server status via maxadmin is not atomic
* [MXS-832](https://jira.mariadb.org/browse/MXS-832) Problem with Regex filter as readconnroute doesn't wait for complete packets
* [MXS-831](https://jira.mariadb.org/browse/MXS-831) new_master event not triggered by galeramon
* [MXS-828](https://jira.mariadb.org/browse/MXS-828) Remove "Syslog logging is disabled." to stdout when starting without syslog
* [MXS-825](https://jira.mariadb.org/browse/MXS-825) --execdir option does not work
* [MXS-805](https://jira.mariadb.org/browse/MXS-805) Server weights don't work with LEAST_BEHIND_MASTER
* [MXS-804](https://jira.mariadb.org/browse/MXS-804) Grants for user@IP/Netmask doesn't work
* [MXS-799](https://jira.mariadb.org/browse/MXS-799) fatal signal 11 when socket could not be opened
* [MXS-769](https://jira.mariadb.org/browse/MXS-769) Malloc return value must be checked.
* [MXS-711](https://jira.mariadb.org/browse/MXS-711) All service ports use the same user data
* [MXS-650](https://jira.mariadb.org/browse/MXS-650) Connection attempt w/o SSL to SSL service gives confusing error
* [MXS-626](https://jira.mariadb.org/browse/MXS-626) Don't log anything to maxlog until it is known whether that is wanted.
* [MXS-590](https://jira.mariadb.org/browse/MXS-590) MaxScale doesn't log an error when .secrets file is not owned by current user
* [MXS-586](https://jira.mariadb.org/browse/MXS-586) Tee filter hangs when using range
* [MXS-576](https://jira.mariadb.org/browse/MXS-576) Maxscale does not generate warning/error if incorrect values is set for persistpoolmax
* [MXS-397](https://jira.mariadb.org/browse/MXS-397) Unsafe handling of dcb_readqueue
* [MXS-390](https://jira.mariadb.org/browse/MXS-390) Lack of checks of dynamic memory allocation
* [MXS-350](https://jira.mariadb.org/browse/MXS-350) Return value of realloc must not be assigned to provided pointer.
* [MXS-348](https://jira.mariadb.org/browse/MXS-348) Incorrect use of strncat
* [MXS-253](https://jira.mariadb.org/browse/MXS-253) Use of strncpy is dangerous
* [MXS-126](https://jira.mariadb.org/browse/MXS-126) debug assert in TEE filter test

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
