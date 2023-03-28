# MariaDB MaxScale 6.4.6 Release Notes -- 2023-03-29

Release 6.4.6 is a GA release.

This document describes the changes in release 6.4.6, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4566](https://jira.mariadb.org/browse/MXS-4566) RHEL8 Packages for 23.02.1 and 22.08.5
* [MXS-4557](https://jira.mariadb.org/browse/MXS-4557) Binlogrouter breaks if event size exceeds INT_MAX
* [MXS-4556](https://jira.mariadb.org/browse/MXS-4556) Maxscale ignores lower_case_table_names=1 on config file
* [MXS-4555](https://jira.mariadb.org/browse/MXS-4555) Dynamic filter capabilities do not work
* [MXS-4552](https://jira.mariadb.org/browse/MXS-4552) "Unknown prepared statement handler" error when connection_keepalive is disabled on a readconnroute service
* [MXS-4547](https://jira.mariadb.org/browse/MXS-4547) Empty regex // is not treated as empty
* [MXS-4540](https://jira.mariadb.org/browse/MXS-4540) transaction replay retries repeatedly after failing checksum
* [MXS-4524](https://jira.mariadb.org/browse/MXS-4524) Wrong server version assumption
* [MXS-4515](https://jira.mariadb.org/browse/MXS-4515) MaxScale leaks sessions if they are closed when writeq throttling is enabled
* [MXS-4514](https://jira.mariadb.org/browse/MXS-4514) skip_name_resolve is not modifiable at runtime
* [MXS-4510](https://jira.mariadb.org/browse/MXS-4510) Uncaught exception in binlogrouter
* [MXS-4504](https://jira.mariadb.org/browse/MXS-4504) IP wildcard values are  not permitted in host values while using data masking
* [MXS-4499](https://jira.mariadb.org/browse/MXS-4499) config_sync_cluster always uses the mysql database
* [MXS-4494](https://jira.mariadb.org/browse/MXS-4494) Replication breaks if binlogfilter excludes events
* [MXS-4489](https://jira.mariadb.org/browse/MXS-4489) PHP program reports different collation_connection when connecting via Maxscale
* [MXS-4481](https://jira.mariadb.org/browse/MXS-4481) Attempting to create a table with the name "DUAL" crashes MaxScale
* [MXS-4476](https://jira.mariadb.org/browse/MXS-4476) Memory leak in smartrouter
* [MXS-4473](https://jira.mariadb.org/browse/MXS-4473) Hang in smartrouter under heavy load
* [MXS-4459](https://jira.mariadb.org/browse/MXS-4459) Improve match/exclude documentation for avrorouter and kafkacdc
* [MXS-4410](https://jira.mariadb.org/browse/MXS-4410) QLA filter not properly logging USE DBx command.
* [MXS-4197](https://jira.mariadb.org/browse/MXS-4197) pinloki_start_stop is unstable
* [MXS-3972](https://jira.mariadb.org/browse/MXS-3972) The rpl_state in binlogrouter is not atomic

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
