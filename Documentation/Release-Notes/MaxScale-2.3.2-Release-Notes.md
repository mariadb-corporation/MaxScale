# MariaDB MaxScale 2.3.2 Release Notes

Release 2.3.2 is a XXX release.

This document describes the changes in release 2.3.2, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

### Watchdog

The systemd watchdog is now safe to use in all circumstances.

By default it is enabled with a timeout of 60 seconds.

### Readwritesplit

#### `connection_keepalive`

The default value of `connection_keepalive` is now 300 seconds. This prevents
the connections from dying due to wait_timeout with longer sessions. This is
especially helpful with pooled connections that stay alive for a very long time.

### MariaDBMonitor

The monitor by default assumes that hostnames used by MaxScale to connect to the backends
are equal to the ones backends use to connect to each other. Specifically, for the slave
connections to be properly detected the `Master_Host` and `Master_Port` fields of the
output to "SHOW ALL SLAVES STATUS"-query must match server entries in the MaxScale
configuration file. If the network configuration is such that this is not the case, the
setting `assume_unique_hostnames` should be disabled.

## Bug fixes

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
