# MariaDB MaxScale 2.1.2 Release Notes

Release 2.1.2 is a Beta release.

This document describes the changes in release 2.1.2, when compared to
release [2.1.1](MaxScale-2.1.1-Release-Notes.md).

If you are upgrading from release 2.0, please also read the following
release notes:
[2.1.1](./MaxScale-2.1.1-Release-Notes.md)
[2.1.0](./MaxScale-2.1.0-Release-Notes.md)

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Cache

* The storage `storage_inmemory` is now the default, so the parameter
  `storage` no longer need to be set explicitly.

### Improved Wildcard Matching

The MySQLAuth module now supports all types of wildcards for both IP addresses
as well as hostnames.

### Configurable Connector-C Plugin Directory

The Connector-C used by MaxScale can now be configured to load authentication
plugins from a specific directory with the new `connector_plugindir`
parameter. Read the [Configuration Guide](../Getting-Started/Configuration-Guide.md)
for more details about this new parameter.

## New Features

### IPv6 Support

MaxScale now supports IPv6 connections on both the client and backend side as
well as being able to listen on IPv6 addresses.

### ReadWriteSplit Connection Keepalive

The _readwritesplit_ module now implements a `connection_keepalive`
feature which allows sending of keepalive pings to idle connections. For
more details, read the [ReadWriteSplit documentation](../Routers/ReadWriteSplit.md).

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 2.1.1.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20resolution%20in%20(Fixed%2C%20Done)%20AND%20fixVersion%20%3D%202.1.2%20AND%20fixVersion%20NOT%20IN%20(2.1.1))

* [MXS-1032](https://jira.mariadb.org/browse/MXS-1032) missing mysql_clear_password.so plugin

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
