# MariaDB MaxScale 2.3.1 Release Notes -- 2018-11-20

Release 2.3.1 is a Beta release.

This document describes the changes in release 2.3.1, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

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

## Deprecated features

The following configuration file options have been deprecated and will
be removed in 2.4.

#### Global section
* `non_blocking_polls`, ignored.
* `poll_sleep`, ignored.
* `thread_stack_size`, ignored.

#### Services and Monitors
* `passwd`, replaced with `password`.

### MaxAdmin

MaxAdmin has been deprecated in favor of the REST API and MaxCtrl. It will be
removed in a later release.

In addition to this the commands `set pollsleep` and `set nbpolls` have been
deprecated and will be removed already in 2.4.

### MaxInfo

The `maxinfo` router has been deprecated and will be removed in a later release.

### Debugcli

The `debugcli` module has been deprecated and will be removed in 2.4.

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

### Watchdog

If MaxScale is running as a systemd service, the systemd Watchdog can be
enabled and MaxScale will behave accordingly. Please see the
[documentation](Getting-Started/Configuration-Guide.md#systemd-watchdog)
for more details.

By default the watchdog is disabled.

*NOTE*: In 2.3.1 there is a deficiency that manifests itself so that if
_any_ administrative operation, performed using _maxctrl_ or _maxadmin_,
takes longer that the specified watchdog timeout, then the watchdog will
kill and restart MaxScale. Please take that into account before enabling
the watchdog.

### Miscellaneous

* [MXS-2141](https://jira.mariadb.org/browse/MXS-2141) Retry read on master when causal_reads timeout is exceeded
* [MXS-2122](https://jira.mariadb.org/browse/MXS-2122) Immediately close the listening socket when a listener is destroyed
* [MXS-2077](https://jira.mariadb.org/browse/MXS-2077) Provide more information in list clients output.
* [MXS-1976](https://jira.mariadb.org/browse/MXS-1976) MaxAdmin Shutting Down A Service should specify / warn that new session requests are neither accepted nor denied.

## Bug fixes

* [MXS-2147](https://jira.mariadb.org/browse/MXS-2147) Luafilter is missing symbols
* [MXS-2144](https://jira.mariadb.org/browse/MXS-2144) Doing a controlled shutdown doesn't trigger query retrying
* [MXS-2142](https://jira.mariadb.org/browse/MXS-2142) Default timeout value for causal_reads is excessive
* [MXS-2140](https://jira.mariadb.org/browse/MXS-2140) Enabling transaction_replay at runtime doesn't enable implicit parameters
* [MXS-2139](https://jira.mariadb.org/browse/MXS-2139) transaction_replay doesn't implicitly enable master_failure_mode=fail_on_write
* [MXS-2136](https://jira.mariadb.org/browse/MXS-2136) passwd errors out as a attribute in [monitor] and [service] in maxscale.cnf
* [MXS-2121](https://jira.mariadb.org/browse/MXS-2121) Listeners defined in the configuration cannot be destroyed
* [MXS-2109](https://jira.mariadb.org/browse/MXS-2109) query_classifier_cache_size is per thread
* [MXS-2107](https://jira.mariadb.org/browse/MXS-2107) writeq_high_water doesn't work
* [MXS-2100](https://jira.mariadb.org/browse/MXS-2100) Unknown global parameters are not detected
* [MXS-2098](https://jira.mariadb.org/browse/MXS-2098) maintenance_on_low_disk_space does not work
* [MXS-2096](https://jira.mariadb.org/browse/MXS-2096) SELECT ... INTO OUTFILE is routed to all back end servers
* [MXS-2055](https://jira.mariadb.org/browse/MXS-2055) Monitor REST-API documentation
* [MXS-1978](https://jira.mariadb.org/browse/MXS-1978) SELECT INTO OUTFILE is routed to all servers

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for supported the Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/mariadb-tx/maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
