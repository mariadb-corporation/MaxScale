# MariaDB MaxScale 6.0.0 Release Notes

The versioning scheme has changed; the major number will now change at
every release which is not only a maintenance release. According to the
old scheme, the version number would have been 2.6.

Release 6.0 is a Beta release.

This document describes the changes in release 6, when compared to
release 2.5.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

### Columnstore monitor

The Columnstore monitor that in this release only supports Columnstore
version 1.5, is now capable of adjusting itself automatically to any
changes in the the cluster. In the configuration it is only specified
a node using which the monitor get in contact with the cluster, but
after that the monitor autonomously figures out the cluster configuration
and automatically adapts to any changes in the configuration. For more
details, please consult the
[monitor](../Monitors/ColumnStore-Monitor.md)
documentation.

## Dropped Features

## Deprecated Features

### Database Firewall filter

The filter is deprecated in MaxScale 6 and will be removed in MaxScale 7.

## New Features

### `nosqlprotocol` protocol module

This module implements a subset of the MongoDB® wire protocol and
transparently converts MongoDB commands into the equivalent SQL
statements that subsequently are executed on a MariaDB server. This
allows client applications utilizing some MongoDB client library to
use a MariaDB server as backend. As the conversion is performed in
the protocol module, this functionality can be used together with
all MaxScale routers and filters. Please see the `nosqlprotocol`
[documentation](../Protocols/NoSQL.md) for more information.

### Defaults can be specified for `maxctrl`

If the file `~/.maxctrl.cnf` exists, maxctrl will use any values
in the section `[maxctrl]` as defaults for command line arguments.
Please see the `maxctrl` [documentation](../Reference/MaxCtrl.md)
for details.

## Bug fixes

* [MXS-3621](https://jira.mariadb.org/browse/MXS-3621) The presence of a filter may cause a nosqlprotocol session to hang
* [MXS-3600](https://jira.mariadb.org/browse/MXS-3600) The cursor must be visible from all connections
* [MXS-3592](https://jira.mariadb.org/browse/MXS-3592) MaxCtrl object creation doesn't convert values to JSON
* [MXS-3537](https://jira.mariadb.org/browse/MXS-3537) Default value of threads is not optimal
* [MXS-3530](https://jira.mariadb.org/browse/MXS-3530) Maxctrl is not buildable with aarch64 and latest nodes
* [MXS-3515](https://jira.mariadb.org/browse/MXS-3515) COM_STMT_EXECUTE with ID -1 isn't handled correctly
* [MXS-3513](https://jira.mariadb.org/browse/MXS-3513) Fix canonical tests to match new get_canonical
* [MXS-3443](https://jira.mariadb.org/browse/MXS-3443) Query performance degradation in 2.5
* [MXS-3294](https://jira.mariadb.org/browse/MXS-3294) Fix module loading
* [MXS-3232](https://jira.mariadb.org/browse/MXS-3232) `test_event`@develop fails on debian
* [MXS-3184](https://jira.mariadb.org/browse/MXS-3184) COM_STMT_EXECUTE with FOUND_ROWS not routed to previous target
* [MXS-3028](https://jira.mariadb.org/browse/MXS-3028) Node wrongly in Maintenance, Running when the node is actually Down
* [MXS-2915](https://jira.mariadb.org/browse/MXS-2915) TLS version not used by mxq::MariaDB

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the supported Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/#mariadb_platform-mariadb_maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
