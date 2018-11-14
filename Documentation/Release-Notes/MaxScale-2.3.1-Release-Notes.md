# MariaDB MaxScale 2.3.1 Release Notes

Release 2.3.1 is a Beta release.

This document describes the changes in release 2.3.1, when compared to
release 2.3.0.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Unknown global parameters

Unknown global parameters or parameters with invalid values are now treated as
errors. If MaxScale refuses to start after upgrading to 2.3.1, check whether it
is due to an unknown global parameter.

### REST-API

#### `/v1/sessions`

The response will, if the feature has been enabled with the
`retain_last_statements` parameter, either globally or specifically
for a service, contain information about the last queries executed
by a session.

### Binlog Router

Secondary masters can now be specified also when file + position
based replication is used. Earlier it was possible only in conjunction
with GTID based replication.

### `mmmon` and `ndbclustermon`

Both of these modules have been deprecated and will be removed in a future
release. The functionality in `mmmon` has been largely obsoleted by the
advancements in `mariadbmon`. The `ndbclustermon` is largely obsolete due to the
fact that there are virtually no users who use it.

### Deprecated options

The following configuration file options have been deprecated and will
be removed in 2.4.

#### Global section
* `non_blocking_polls`, ignored.
* `poll_sleep`, ignored.
* `thread_stack_size`, ignored.

#### Services and Monitors
* `passwd`, replaced with `password`.

### MaxAdmin

The commands `set pollsleep` and `set nbpolls` have been deprecated and
will be removed in 2.4.

## New Features

### ColumnStore Monitor

The new `csmon` monitor can be used to monitor ColumnStore clusters where the
primary UM will be assigned as the master and secondary UMs as slaves. Automatic
detection of the primary UM is supported with ColumnStore versions 1.2.1 and
newer. For older versions the primary UM must be designated with the `primary`
parameter of the monitor.

Read the [csmon documentation](../Monitors/ColumnStore-Monitor.md) for more
information on how to use it.

### MaxCtrl

There is now a new command `classify <statement>` using which it can
be checked if and how MaxScale classifies a specific statement. This
feature can be used for debugging, if there is suspicion that MaxScale
sends a particular statement to the wrong server (e.g. to a slave when it
should be sent to the master).

### Services

The global configuration parameter `retain_last_statements` can now
also be specified separately for individual services.

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.3.1.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.3.1)

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
