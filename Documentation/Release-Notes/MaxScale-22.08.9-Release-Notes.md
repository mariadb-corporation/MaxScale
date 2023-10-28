# MariaDB MaxScale 22.08.9 Release Notes

Release 22.08.9 is a GA release.

This document describes the changes in release 22.08.9, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-22.08.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## External CVEs resolved.

* [CVE-2022-1586](https://www.cve.org/CVERecord?id=CVE-2022-1586) Fixed by [MXS-4806](https://jira.mariadb.org/browse/MXS-4806) Update pcre2 to 10.42
* [CVE-2022-1587](https://www.cve.org/CVERecord?id=CVE-2022-1587) Fixed by [MXS-4806](https://jira.mariadb.org/browse/MXS-4806) Update pcre2 to 10.42
* [CVE-2022-41409](https://www.cve.org/CVERecord?id=CVE-2022-41409) Fixed by [MXS-4806](https://jira.mariadb.org/browse/MXS-4806) Update pcre2 to 10.42
* [CVE-2020-7105](https://www.cve.org/CVERecord?id=CVE-2020-7105) Fixed by [MXS-4757](https://jira.mariadb.org/browse/MXS-4757) Update libhiredis to 1.0.2.
* [CVE-2023-27371](https://www.cve.org/CVERecord?id=CVE-2023-27371) Fixed by [MXS-4751](https://jira.mariadb.org/browse/MXS-4751) Update libmicrohttpd to version 0.9.76

## Bug fixes

* [MXS-4831](https://jira.mariadb.org/browse/MXS-4831) Missing SQL error in server state change messages
* [MXS-4815](https://jira.mariadb.org/browse/MXS-4815) @@last_gtid and @@last_insert_id are treated differently
* [MXS-4814](https://jira.mariadb.org/browse/MXS-4814) GTIDs used by causal_reads=global cannot be reset without restarting MaxScale
* [MXS-4812](https://jira.mariadb.org/browse/MXS-4812) More than one primary database in a monitor results in errors in MaxScale GUI
* [MXS-4811](https://jira.mariadb.org/browse/MXS-4811) Error handling differences between running maxctrl directly or in a subshell
* [MXS-4810](https://jira.mariadb.org/browse/MXS-4810) --timeout doesn't work with multiple values in --hosts
* [MXS-4807](https://jira.mariadb.org/browse/MXS-4807) MaxScale does not always report the OS version correctly
* [MXS-4799](https://jira.mariadb.org/browse/MXS-4799) ConfigManager may spam the log with warnings
* [MXS-4797](https://jira.mariadb.org/browse/MXS-4797) NullFilter has not been extended to support all routing enumeration values.
* [MXS-4792](https://jira.mariadb.org/browse/MXS-4792) Semi-sync replication through MaxScale causes errors on STOP SLAVE
* [MXS-4790](https://jira.mariadb.org/browse/MXS-4790) Log version after log rotation
* [MXS-4788](https://jira.mariadb.org/browse/MXS-4788) Galeramon should use gtid_binlog_pos if gtid_current_pos is empty
* [MXS-4782](https://jira.mariadb.org/browse/MXS-4782) Kafkacdc logs warnings about the configuration
* [MXS-4781](https://jira.mariadb.org/browse/MXS-4781) cooperative_replication works even if cluster parameter is not used
* [MXS-4780](https://jira.mariadb.org/browse/MXS-4780) Shutdown may hang if cooperative_replication is used
* [MXS-4778](https://jira.mariadb.org/browse/MXS-4778) Aborts due to SystemD watchdog should tell if a DNS lookup was in progress
* [MXS-4777](https://jira.mariadb.org/browse/MXS-4777) Maxscale crash due to systemd timeout
* [MXS-4775](https://jira.mariadb.org/browse/MXS-4775) KafkaCDC: current_gtid.txt is moving but is behind
* [MXS-4772](https://jira.mariadb.org/browse/MXS-4772) Config sync status leaves origin field empty on restart
* [MXS-4771](https://jira.mariadb.org/browse/MXS-4771) Problem while linking libnosqlprotocol.so
* [MXS-4766](https://jira.mariadb.org/browse/MXS-4766) maxctrl create report cannot write to a pipe
* [MXS-4765](https://jira.mariadb.org/browse/MXS-4765) Serialization of regular expressions doesn't add escaping slashes
* [MXS-4760](https://jira.mariadb.org/browse/MXS-4760) Automatically ignored tables are not documented for schemarouter
* [MXS-4749](https://jira.mariadb.org/browse/MXS-4749) log_throttling should be disabled if log_info is on
* [MXS-4747](https://jira.mariadb.org/browse/MXS-4747) log_throttling is hard to modify via MaxCtrl
* [MXS-4738](https://jira.mariadb.org/browse/MXS-4738) The fact that disable_master_failback does not work with root_node_as_master is not documented
* [MXS-4736](https://jira.mariadb.org/browse/MXS-4736) Read-only transaction sometimes loses statements with causal_reads=universal and transaction_replay=true
* [MXS-4735](https://jira.mariadb.org/browse/MXS-4735) Connection IDs are missing from error messages
* [MXS-4734](https://jira.mariadb.org/browse/MXS-4734) SET TRANSACTION READ ONLY is classified as a session command
* [MXS-4732](https://jira.mariadb.org/browse/MXS-4732) MaxScale shutdown is not signal-safe
* [MXS-4724](https://jira.mariadb.org/browse/MXS-4724) slave_selection_criteria should accept lowercase version of the values
* [MXS-4707](https://jira.mariadb.org/browse/MXS-4707) The match parameters are not regular expressions
* [MXS-4616](https://jira.mariadb.org/browse/MXS-4616) Limit the number of statements to be executed in the Query Editor
* [MXS-4562](https://jira.mariadb.org/browse/MXS-4562) When MaxScalle is installed from tarball and starded without -d option --basedir=.  is not parsed properly and full directory needs to be specified
* [MXS-4538](https://jira.mariadb.org/browse/MXS-4538) No valid servers in cluster 'MariaDB-Monitor'
* [MXS-4457](https://jira.mariadb.org/browse/MXS-4457) Duplicate values in `servers` are silently ignored

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
