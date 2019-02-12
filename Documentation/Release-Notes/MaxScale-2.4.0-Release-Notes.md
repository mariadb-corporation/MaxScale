# MariaDB MaxScale 2.4.0 Release Notes

Release 2.4.0 is a Beta release.

This document describes the changes in release 2.4.0, when compared to
release 2.3.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Section and object names

Section and object names starting with `@@` are now reserved for
use by MaxScale itself. If any such names are encountered in
configuration files, then MaxScale will not start.

Whitespace in section names that was deprecated in 2.2 will now be
rejected and cause the startup of MaxScale to fail.

## Dropped Features

## New Features

### Servers can be drained

It is now possible to drain a server, which means that existing
connections to the server can continue to be used but new connections
are no longer created to the server.

In the output of `maxctrl`, the fact that a server is being drained
is visible in the `State` column as the value `Being Drained`.
```
┌─────────┬─────────────────┬──────┬─────────────┬───────────────────────────────┬───────┐
│ Server  │ Address         │ Port │ Connections │ State                         │ GTID  │
├─────────┼─────────────────┼──────┼─────────────┼───────────────────────────────┼───────┤
│ Server1 │ 192.168.121.159 │ 3306 │ 2           │ Master, Running               │ 0-1-6 │
├─────────┼─────────────────┼──────┼─────────────┼───────────────────────────────┼───────┤
│ Server2 │ 192.168.121.80  │ 3306 │ 1           │ Being Drained, Slave, Running │ 0-1-6 │
├─────────┼─────────────────┼──────┼─────────────┼───────────────────────────────┼───────┤
│ Server3 │ 192.168.121.122 │ 3306 │ 2           │ Slave, Running                │ 0-1-6 │
├─────────┼─────────────────┼──────┼─────────────┼───────────────────────────────┼───────┤
│ Server4 │ 192.168.121.144 │ 3306 │ 2           │ Slave, Running                │ 0-1-6 │
└─────────┴─────────────────┴──────┴─────────────┴───────────────────────────────┴───────┘
```
A server is set in the _being drained_ state the same way as it is
set in the _maintenance_ state:
```
$ maxctrl set server Server2 drain
```
Note that although the state is displayed as `Being Drained`, when setting
and clearing the state, the word `drain` is used.

Note that the full implication of draining a server depends upon
both on the role of the server and on the router being used, and its
configuration.

For instance, if readwritesplit is used and the server being drained
is a slave, then from a client's perspective there will be no difference;
readwritesplit will simply not use that server. However, if the server
being drained is the master, then it will not be possible to connect
unless `master_failure_mode` has been set to something else but the
default `fail_instantly`.

### Cluster

The servers a service uses can now be specified using the `cluster`
parameter of the service.
```
[TheService]
...
cluster=TheMonitor
```
In this case, the servers of the service will be defined by the
referred to monitor. Note that the parameters `servers` and `cluster`
are mutually exclusive.

### Durations

In the MaxScale configuration file, durations can now be suffixed with
`h`, `m`, `s` or `ms` to indicate that the duration is specified as
hours, minutes, seconds or milliseconds.

Please see the
[configuration guide](../Getting-Started/Configuration-Guide.md#durations)
for details.

_Not_ providing an explicit unit has been deprecated in MaxScale 2.4.

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.4.0.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.4.0)

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
