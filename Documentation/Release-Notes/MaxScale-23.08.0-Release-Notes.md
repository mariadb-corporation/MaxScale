# MariaDB MaxScale 23.08 Release Notes -- 2023-08-

Release 23.08.0 is a Beta release.

This document describes the changes in release 23.08, when compared to
release 23.02.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

### [MXS-4385](https://jira.mariadb.org/browse/MXS-4385) No newlines in logged messages

Earlier when the SQL sent by a client was logged due to `log_info` being enabled,
the SQL was logged verbatim, which would cause the log to contain extra newlines
in case the SQL did. From 23.08 forward, newlines are replaced with the text `\n`,
so that a logged line will not contain any extra newlines.

## Dropped Features

###

## Deprecated Features

   * The configuration parameters `query_classifier` and `query_classifier_args`
     have been deprecated and are ignored.

## New Features

### [MXS-3664](https://jira.mariadb.org/browse/MXS-3664) Built-in caching in nosqlprotocol

The NoSQL protocol module now supports internal caching. Since this
cache uses keys created from NoSQL protocol requests and stores NoSQL
protocol responses, it is more efficient than the regular cache filter.
More information about this functionality can be found
[here](../Protocols/NoSQL.md#caching).

### [MXS-4232](https://jira.mariadb.org/browse/MXS-4232) Remember old service password

When the service password is changed, MaxScale will remember and use the previous
password if the new does not work. This makes it easier to manage the changing of
the password, as the password in the backend and in MaxScale need not be changed
simultaneously. More information about this functionality can be found
[here](../Getting-Started/Configuration-Guide.md#user-and-password).

### [MXS-4377](https://jira.mariadb.org/browse/MXS-4377) Common options

It is now possible to specify options in an _include_-section, to be included
by other sections. This is useful, for instance, if there are multiple monitors
that otherwise are identically configured, but for their list of servers. More
information about this functionality can be found
[here](../Getting-Started/Configuration-Guide.md#include-1).

### [MXS-4549](https://jira.mariadb.org/browse/MXS-4549) Replay queries with partially returned results

If a query in a transaction is interrupted and the result was partially
delivered to the client, readwritesplit will now retry the execution of the
query and discard the already delivered part of the result.

### [MXS-4215](https://jira.mariadb.org/browse/MXS-4215) Allow manual clearing of schemarouter caches

The schemarouter database map cache can now be manually cleared with a MaxCtrl
command:

```
maxctrl call command schemarouter clear <service>
```

This makes it possible to schedule the clearing of the caches for busy systems
where the update of the map takes a long time.

### [MXS-4216](https://jira.mariadb.org/browse/MXS-4216) Allow stale cache usage in schemarouter

Stale entries in the schemarouter database map can now be used up to
`max_staleness` seconds. This reduces the impact that a shard update causes to
the client applications.

### [MXS-3983](https://jira.mariadb.org/browse/MXS-3983) Add switchover-force command

This version of switchover performs the switch even if the primary server is
unresponsive i.e. responds to pings but does not perform any commands in a
reasonable time. May lead to diverging replication on the old primary.

### MaxGUI


## Bug fixes

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
