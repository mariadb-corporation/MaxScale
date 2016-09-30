# MariaDB MaxScale 2.1.0 Release Notes

Release 2.1.0 is a Beta release.

This document describes the changes in release 2.1.0, when compared to
release 2.0.X.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changes Features

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

### User data cache

The user data loaded from the backend databases is now stored on a per listener
basis instead of a per service basis. In earlier versions, each service had its own
cache directory in `/var/cache/maxscale`. This directory contains cached user
data which is used there is no connectivity to the backend cluster.

In 2.1.0, each listener has its own sub-directory in the service cache
directory. The old caches in `/var/cache/maxscale` will need to be manually
removed if they are no longer used by older versions of MaxScale.

## New Features

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
