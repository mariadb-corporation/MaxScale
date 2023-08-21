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

### [MXS-2744](https://jira.mariadb.org/browse/MXS-2744) Switchover improvements

During switchover, MariaDB-Monitor creates a new connection to the master with
a long timeout, ignoring the limit of `backend_read_timeout`. This reduces the
probability of long commands such as `set global read_only=1` timing out. When
kicking out super and read-only admin users, monitor prevent writes with
"flush tables with read lock". See
[monitor documentation](../Monitors/MariaDB-Monitor.md#operation-details)
for more information.

## Dropped Features

###

## Deprecated Features

* The configuration parameters `query_classifier` and `query_classifier_args`
  have been deprecated and are ignored.

* The `strip_db_esc` parameter is deprecated and will be removed in a future
  release. The default behavior of stripping escape characters is in all known
  cases the correct thing to do and as such this parameter is never required.

## New Features

### [MXS-3531](https://jira.mariadb.org/browse/MXS-3531) Lower regular expression matching limits

The PCRE2 library used by MaxScale now limits the heap memory to 1GB and the
matching limit to 500000 matches. This change was done to prevent catastrophic
backtracing that occurred when regular expressions used nested recursion
e.g. `SELECT.*.*FROM.*.*t1`.

### [MXS-3664](https://jira.mariadb.org/browse/MXS-3664) Built-in caching in nosqlprotocol

The NoSQL protocol module now supports internal caching. Since this
cache uses keys created from NoSQL protocol requests and stores NoSQL
protocol responses, it is more efficient than the regular cache filter.
More information about this functionality can be found
[here](../Protocols/NoSQL.md#caching).

### [MXS-3983](https://jira.mariadb.org/browse/MXS-3983) Add switchover-force command

This version of switchover performs the switch even if the primary server is
unresponsive i.e. responds to pings but does not perform any commands in a
reasonable time. May lead to diverging replication on the old primary. See
[monitor documentation](../Monitors/MariaDB-Monitor.md#operation-details)
for more information.

### [MXS-4123](https://jira.mariadb.org/browse/MXS-4123) Fast universal causal reads

The `causal_reads=fast_universal` mode uses the same mechanism to retrieve the
GTID position that the `universal` mode uses but behaves like the `fast` mode
when routing queries.

### [MXS-4215](https://jira.mariadb.org/browse/MXS-4215) Manual schemarouter cache invalidation

The schemarouter database map cache can now be manually cleared with a MaxCtrl
command:

```
maxctrl call command schemarouter clear <service>
```

This makes it possible to schedule the clearing of the caches for busy systems
where the update of the map takes a long time.

### [MXS-4216](https://jira.mariadb.org/browse/MXS-4216) Stale cache usage in schemarouter

Stale entries in the schemarouter database map can now be used up to
`max_staleness` seconds. This reduces the impact that a shard update causes to
the client applications.

### [MXS-4232](https://jira.mariadb.org/browse/MXS-4232) Remember old service password

When the service password is changed, MaxScale will remember and use the previous
password if the new does not work. This makes it easier to manage the changing of
the password, as the password in the backend and in MaxScale need not be changed
simultaneously. More information about this functionality can be found
[here](../Getting-Started/Configuration-Guide.md#user-and-password).

### [MXS-4277](https://jira.mariadb.org/browse/MXS-4277) Configurable `iss` field in JWTs

The `iss` field of the JWTs that the REST-API in MaxScale generates can now be
configured with `admin_jwt_issuer`. This allows REST-API clients to see who
issued the token.

### [MXS-4377](https://jira.mariadb.org/browse/MXS-4377) Common options

It is now possible to specify options in an _include_-section, to be included
by other sections. This is useful, for instance, if there are multiple monitors
that otherwise are identically configured, but for their list of servers. More
information about this functionality can be found
[here](../Getting-Started/Configuration-Guide.md#include-1).

### [MXS-4505](https://jira.mariadb.org/browse/MXS-4505) Safer replaying of about-to-commit transactions

The new `transaction_replay_safe_commit` variable controls whether
about-to-commit transaction are replayed when `transaction_replay` is
enabled. The new default is to not replay transactions that were being committed
when the connection to the server was lost.

### [MXS-4549](https://jira.mariadb.org/browse/MXS-4549) Queries with partially returned results can be replayed

If a query in a transaction is interrupted and the result was partially
delivered to the client, readwritesplit will now retry the execution of the
query and discard the already delivered part of the result.

### [MXS-4618](https://jira.mariadb.org/browse/MXS-4618) LOAD DATA LOCAL INFILE from S3

The [LDI filter](../Filters/Ldi.md) adds support for `LOAD DATA LOCAL INFILE`
from S3 compatible storage.

### [MXS-4635](https://jira.mariadb.org/browse/MXS-4635) Early connection metadata

The new `connection_metadata` listener parameter controls the set of metadata
variables that are sent to the client during connection creation.

By default the values of the system variables `character_set_client`,
`character_set_connection`, `character_set_results`, `max_allowed_packet`,
`system_time_zone`, `time_zone` and `tx_isolation` are sent as well as the
current number of connections as the `threads_connected` variable and the real
64-bit connection ID as `connection_id`.

Compatible MariaDB connectors will use this information from MaxScale instead of
querying the values of the variables from the database server which greatly
speeds up connection creation.

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
