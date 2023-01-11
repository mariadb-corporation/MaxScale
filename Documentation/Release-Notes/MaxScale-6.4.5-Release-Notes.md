# MariaDB MaxScale 6.4.5 Release Notes

Release 6.4.5 is a GA release.

This document describes the changes in release 6.4.5, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4474](https://jira.mariadb.org/browse/MXS-4474) MaxScale hangs with warning about "Worker 1 attempted to send a message to worker 1"
* [MXS-4471](https://jira.mariadb.org/browse/MXS-4471) Table selection doesn't tolerate node failures
* [MXS-4470](https://jira.mariadb.org/browse/MXS-4470) COM_INIT_DB isn't routed to all shards
* [MXS-4469](https://jira.mariadb.org/browse/MXS-4469) Schemarouter routing logic documentation is out of date
* [MXS-4467](https://jira.mariadb.org/browse/MXS-4467) Explicit transactions without a default database do not work as expected with schemarouter
* [MXS-4460](https://jira.mariadb.org/browse/MXS-4460) Crash during query replay with service-to-service configuration
* [MXS-4454](https://jira.mariadb.org/browse/MXS-4454) Schemarouter should prefer targets which have databases in them for session commands
* [MXS-4453](https://jira.mariadb.org/browse/MXS-4453) Schemarouter selects an invalid target for queries that do not target a specific shard
* [MXS-4450](https://jira.mariadb.org/browse/MXS-4450) 6.4 no longer provides full certificate chain in TLS HELLO
* [MXS-4440](https://jira.mariadb.org/browse/MXS-4440) Lost connection to backend server: network error (server1: 104, Connection reset by peer)
* [MXS-4439](https://jira.mariadb.org/browse/MXS-4439) Maxscale is failing with Resource temporarily unavailable errors
* [MXS-4435](https://jira.mariadb.org/browse/MXS-4435) Log rotation causes errors in qlafilter
* [MXS-4423](https://jira.mariadb.org/browse/MXS-4423) Rebalancing is not always initiated from the affected worker/thread

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
