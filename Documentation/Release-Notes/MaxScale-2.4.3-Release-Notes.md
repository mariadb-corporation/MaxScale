# MariaDB MaxScale 2.4.3 Release Notes

Release 2.4.3 is a GA release.

This document describes the changes in release 2.4.3, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2734](https://jira.mariadb.org/browse/MXS-2734) Unexpected error message when preferred_server is configured
* [MXS-2733](https://jira.mariadb.org/browse/MXS-2733) UTC_TIMESTAMP not recognized as a built-in function
* [MXS-2732](https://jira.mariadb.org/browse/MXS-2732) Memory leak with QC
* [MXS-2731](https://jira.mariadb.org/browse/MXS-2731) Crash in Avro router.
* [MXS-2728](https://jira.mariadb.org/browse/MXS-2728) maxscale .secret file permission
* [MXS-2721](https://jira.mariadb.org/browse/MXS-2721) MaxScale 2.4.2 crashes on different lxc containers with similar error
* [MXS-2720](https://jira.mariadb.org/browse/MXS-2720) maxctrl and maxadmin report negative number of connections to a service
* [MXS-2713](https://jira.mariadb.org/browse/MXS-2713) SET PASSWORD statement sent to all nodes, not only to master
* [MXS-2711](https://jira.mariadb.org/browse/MXS-2711) retain_last_statements cannot be disabled at runtime
* [MXS-2707](https://jira.mariadb.org/browse/MXS-2707) MaxScale 2.4.2 crashes randomly
* [MXS-2702](https://jira.mariadb.org/browse/MXS-2702) Session command with lazy_connect causes master to be "lost"
* [MXS-2699](https://jira.mariadb.org/browse/MXS-2699) Two QC bugs
* [MXS-2690](https://jira.mariadb.org/browse/MXS-2690) Schemarouter doesn't detect empty duplicate databases
* [MXS-2688](https://jira.mariadb.org/browse/MXS-2688) SET should be classified as QUERY_TYPE_SESSION_WRITE
* [MXS-2687](https://jira.mariadb.org/browse/MXS-2687) Invalid socket paths not treated as errors
* [MXS-2684](https://jira.mariadb.org/browse/MXS-2684) Backend DCB taken from persistent pool is not subject to throttling
* [MXS-2675](https://jira.mariadb.org/browse/MXS-2675) TLS not enabled after server creation via maxctrl
* [MXS-2674](https://jira.mariadb.org/browse/MXS-2674) QC bugs
* [MXS-2672](https://jira.mariadb.org/browse/MXS-2672) MaxScale 2.4.2 keep crashing
* [MXS-2664](https://jira.mariadb.org/browse/MXS-2664) Nagios not found on MaxScale 2.4
* [MXS-2652](https://jira.mariadb.org/browse/MXS-2652) Maintenance mode lost if node is shutdown
* [MXS-2620](https://jira.mariadb.org/browse/MXS-2620) Document that shutting down the master can break auto_rejoin and lose transactions
* [MXS-2600](https://jira.mariadb.org/browse/MXS-2600) Documentation is inconsistent regarding privileges for MariaDB Monitor
* [MXS-2564](https://jira.mariadb.org/browse/MXS-2564) Rapid reconnection on server failure
* [MXS-2354](https://jira.mariadb.org/browse/MXS-2354) Timestamp/Datetime precision lost when using cdc streaming
* [MXS-2264](https://jira.mariadb.org/browse/MXS-2264) Schema aware avrorouter ignores table renames
* [MXS-2263](https://jira.mariadb.org/browse/MXS-2263) Maxscale CDC treats unsigned columns as signed
* [MXS-2191](https://jira.mariadb.org/browse/MXS-2191) Split-brain detection doesn't work

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for supported the Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/#mariadb_platform-mariadb_maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
