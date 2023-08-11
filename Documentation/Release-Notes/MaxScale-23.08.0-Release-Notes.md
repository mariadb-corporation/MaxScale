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

### `connection_timeout` renamed to `wait_timeout`

The `connection_timeout` parameter was renamed to `wait_timeout` and the old
name is now an alias to the new name. The use of the old name is deprecated.

### `max_slave_replication_lag` renamed to `max_replication_lag`

The _Readwritesplit_ `max_slave_replication_lag` parameter was renamed to
`max_replication_lag` and the old name is now an alias for the new name.
The use of the old name is deprecated.

## Dropped Features

###

## Deprecated Features

* The configuration parameters `query_classifier` and `query_classifier_args`
  have been deprecated and are ignored.

* The `strip_db_esc` parameter is deprecated and will be removed in a future
  release. The default behavior of stripping escape characters is in all known
  cases the correct thing to do and as such this parameter is never required.

## New Features

### [MXS-2744](https://jira.mariadb.org/browse/MXS-2744) Switchover improvements

### [MXS-3531](https://jira.mariadb.org/browse/MXS-3531) Regex matchin for SQL has no hard limits

### [MXS-3664](https://jira.mariadb.org/browse/MXS-3664) Built-in caching in nosqlprotocol

The NoSQL protocol module now supports internal caching. Since this
cache uses keys created from NoSQL protocol requests and stores NoSQL
protocol responses, it is more efficient than the regular cache filter.
More information about this functionality can be found
[here](../Protocols/NoSQL.md#caching).

### [MXS-3983](https://jira.mariadb.org/browse/MXS-3983) Add switchover-force command

This version of switchover performs the switch even if the primary server is
unresponsive i.e. responds to pings but does not perform any commands in a
reasonable time. May lead to diverging replication on the old primary.

### [MXS-4123](https://jira.mariadb.org/browse/MXS-4123) Fast universal causal reads

### [MXS-4214](https://jira.mariadb.org/browse/MXS-4214) Allow schemarouter caches to be refesh during off-peak hours

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

### [MXS-4226](https://jira.mariadb.org/browse/MXS-4226) Force a Switchover

### [MXS-4232](https://jira.mariadb.org/browse/MXS-4232) Remember old service password

When the service password is changed, MaxScale will remember and use the previous
password if the new does not work. This makes it easier to manage the changing of
the password, as the password in the backend and in MaxScale need not be changed
simultaneously. More information about this functionality can be found
[here](../Getting-Started/Configuration-Guide.md#user-and-password).

### [MXS-4277](https://jira.mariadb.org/browse/MXS-4277) iss filed in JWI tokens is always "maxscale"

### [MXS-4377](https://jira.mariadb.org/browse/MXS-4377) Common options

It is now possible to specify options in an _include_-section, to be included
by other sections. This is useful, for instance, if there are multiple monitors
that otherwise are identically configured, but for their list of servers. More
information about this functionality can be found
[here](../Getting-Started/Configuration-Guide.md#include-1).

### [MXS-4505](https://jira.mariadb.org/browse/MXS-4505) Prevent replaying about-to-be-committed transactions

### [MXS-4549](https://jira.mariadb.org/browse/MXS-4549) Replay queries with partially returned results

If a query in a transaction is interrupted and the result was partially
delivered to the client, readwritesplit will now retry the execution of the
query and discard the already delivered part of the result.

### [MXS-4618](https://jira.mariadb.org/browse/MXS-4618) Load data from S3

### [MXS-4635](https://jira.mariadb.org/browse/MXS-4635) Send metadata in connection handshake

The new `connection_metadata` listener parameter controls the set of metadata
variables that is sent to the client during connection creation. By default the
current number of connections (`threads_connected`) is sent.

### [MXS-4637](https://jira.mariadb.org/browse/MXS-4637) Bootstrap process for Xpand should be region-aware

It is now possible to limit the nodes the Xpand monitor dynamically detects
to those residing in a specific region. See [region_name](../Monitors/Xpand-Monitor.md#region_name)
and [region_oid](../Monitors/Xpand-Monitor.md#region_oid) for more information.

### [MXS-4506](https://jira.mariadb.org/browse/MXS-4506) Add passthrough authentication support for Xpand LDAP

Passthrough authentication mode for MariaDBAuth-module. See
[authenticator documentation](../Authenticators/MySQL-Authenticator.md#clear_pw_passthrough) for more
information.

### MaxGUI
Numerous additions have been added and improvements made to MaxGUI.
The most notable ones are listed here:

* [MXS-3735](https://jira.mariadb.org/browse/MXS-3735) Add ERD modeler to the
workspace. Instructions on using it can be found [here](../Tutorials/Using-MaxGUI-Tutorial.md#create-an-erd).

* [MXS-3991](https://jira.mariadb.org/browse/MXS-3991) Show schema objects
insights. Instructions on using it can be found [here](../Tutorials/Using-MaxGUI-Tutorial.md#show-object-creation-statement-and-insights-info).

* [MXS-4364](https://jira.mariadb.org/browse/MXS-4364) Auto choose active schema for new query tab.

## Bug fixes

### [MXS-4477](https://jira.mariadb.org/browse/MXS-4477) Dashboard graphs refresh unnecessarily

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
