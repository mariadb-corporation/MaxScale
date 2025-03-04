# MariaDB MaxScale 21.06.19 Release Notes

Release 21.06.19 is a GA release.

This document describes the changes in release 21.06.19, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-21.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## External CVEs resolved.

* [CVE-2024-21538](https://www.cve.org/CVERecord?id=CVE-2024-21538) Fixed by [MXS-5478](https://jira.mariadb.org/browse/MXS-5478) Container Image vulnerability CVE-2024-21538 (MXS)

## Bug fixes

* [MXS-5529](https://jira.mariadb.org/browse/MXS-5529) Session commands with max_slave_connections=0 after switchover do not discard stale connections
* [MXS-5527](https://jira.mariadb.org/browse/MXS-5527) The "INSERT INTO...RETURNING" syntax breaks causal_reads
* [MXS-5519](https://jira.mariadb.org/browse/MXS-5519) Documentation regarding mixing of cooperative_monitoring_locks and passive is unclear
* [MXS-5508](https://jira.mariadb.org/browse/MXS-5508) Relationship selections auto-cleared when creating a new monitor object
* [MXS-5507](https://jira.mariadb.org/browse/MXS-5507) readwritesplit enables multi-statements regardless of the state of causal_reads
* [MXS-5492](https://jira.mariadb.org/browse/MXS-5492) idle_session_pool_time=0s does not fairly share connections
* [MXS-5488](https://jira.mariadb.org/browse/MXS-5488) Need Documentation updates for Maxscale install recommendation
* [MXS-5466](https://jira.mariadb.org/browse/MXS-5466) MaxCtrl warnings are very verbose
* [MXS-5455](https://jira.mariadb.org/browse/MXS-5455) Errors during loading of users lack the service name
* [MXS-5450](https://jira.mariadb.org/browse/MXS-5450) maxctrl list queries fails
* [MXS-5449](https://jira.mariadb.org/browse/MXS-5449) Encrypted passwords cannot be used with maxctrl
* [MXS-5443](https://jira.mariadb.org/browse/MXS-5443) Log message: Unknown prepared statement handler given to MaxScale
* [MXS-5439](https://jira.mariadb.org/browse/MXS-5439) Backend connections with fail with EAGAIN
* [MXS-5437](https://jira.mariadb.org/browse/MXS-5437) Failed authentication warnings do not mention lack of client-side SSL as the reason of the failure
* [MXS-5432](https://jira.mariadb.org/browse/MXS-5432) MaxScale 24.02.04 not closing DB Connections properly
* [MXS-5419](https://jira.mariadb.org/browse/MXS-5419) Duration types that only take seconds return ms as units instead of s
* [MXS-5415](https://jira.mariadb.org/browse/MXS-5415) retry_failed_reads is not affected by delayed_retry_timeout
* [MXS-5403](https://jira.mariadb.org/browse/MXS-5403) Debug assertion on very large binary protocol prepared statements
* [MXS-5398](https://jira.mariadb.org/browse/MXS-5398) Some log messages are not logged when session_trace is used
* [MXS-5397](https://jira.mariadb.org/browse/MXS-5397) NVL and NVL2 are not detected as builtin functions outside of sql_mode=ORACLE
* [MXS-5395](https://jira.mariadb.org/browse/MXS-5395) Kafkacdc errors for wrong GTID positions are not clear
* [MXS-5382](https://jira.mariadb.org/browse/MXS-5382) Errors due to max_connections being exceeded are always fatal errors

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
