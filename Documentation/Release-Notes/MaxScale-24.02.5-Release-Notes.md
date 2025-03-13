# MariaDB MaxScale 24.02.5 Release Notes

Release 24.02.5 is a GA release.

This document describes the changes in release 24.02.5, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-24.02.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-5533](https://jira.mariadb.org/browse/MXS-5533) Remove session_trace parameter from services section
* [MXS-5531](https://jira.mariadb.org/browse/MXS-5531) Binary tarballs include experimental modules
* [MXS-5530](https://jira.mariadb.org/browse/MXS-5530) Default value of wait_timeout should be the same as in MariaDB
* [MXS-5529](https://jira.mariadb.org/browse/MXS-5529) Session commands with max_slave_connections=0 after switchover do not discard stale connections
* [MXS-5527](https://jira.mariadb.org/browse/MXS-5527) The "INSERT INTO...RETURNING" syntax breaks causal_reads
* [MXS-5522](https://jira.mariadb.org/browse/MXS-5522) config sync does not ignore port for listeners
* [MXS-5520](https://jira.mariadb.org/browse/MXS-5520) config_sync_password infinitely doubles after maxscale restart & alter command
* [MXS-5519](https://jira.mariadb.org/browse/MXS-5519) Documentation regarding mixing of cooperative_monitoring_locks and passive is unclear
* [MXS-5518](https://jira.mariadb.org/browse/MXS-5518) Documentation of switchover-force lacks warnings
* [MXS-5516](https://jira.mariadb.org/browse/MXS-5516) gtid_start_pos=newest is not documented for avrorouter
* [MXS-5511](https://jira.mariadb.org/browse/MXS-5511) The Contact view in the GUI has outdated information
* [MXS-5508](https://jira.mariadb.org/browse/MXS-5508) Relationship selections auto-cleared when creating a new monitor object
* [MXS-5507](https://jira.mariadb.org/browse/MXS-5507) readwritesplit enables multi-statements regardless of the state of causal_reads
* [MXS-5505](https://jira.mariadb.org/browse/MXS-5505) Revert 60400ee256 MXS-4685: Seek GTIDs incrementally
* [MXS-5493](https://jira.mariadb.org/browse/MXS-5493) Cluster tree is not visualized accurately
* [MXS-5492](https://jira.mariadb.org/browse/MXS-5492) idle_session_pool_time=0s does not fairly share connections
* [MXS-5488](https://jira.mariadb.org/browse/MXS-5488) Need Documentation updates for Maxscale install recommendation
* [MXS-5485](https://jira.mariadb.org/browse/MXS-5485) Reads are not retried if connection creation fails due to sub-service failure
* [MXS-5484](https://jira.mariadb.org/browse/MXS-5484) Binlogrouter multidomain support broken in 24.02
* [MXS-5481](https://jira.mariadb.org/browse/MXS-5481) Galera Monitor does not log an error if "SHOW SLAVE STATUS" fails
* [MXS-5480](https://jira.mariadb.org/browse/MXS-5480) disable_sescmd_history=true causes a use-after-free
* [MXS-5471](https://jira.mariadb.org/browse/MXS-5471) GUI reference doc links are broken
* [MXS-5466](https://jira.mariadb.org/browse/MXS-5466) MaxCtrl warnings are very verbose
* [MXS-5463](https://jira.mariadb.org/browse/MXS-5463) GUI Logs Archive doesn't display latest log on second visit without refresh
* [MXS-5462](https://jira.mariadb.org/browse/MXS-5462) Preserve timestamp when compressing files in Binlogrouter
* [MXS-5455](https://jira.mariadb.org/browse/MXS-5455) Errors during loading of users lack the service name
* [MXS-5450](https://jira.mariadb.org/browse/MXS-5450) maxctrl list queries fails
* [MXS-5449](https://jira.mariadb.org/browse/MXS-5449) Encrypted passwords cannot be used with maxctrl
* [MXS-5443](https://jira.mariadb.org/browse/MXS-5443) Log message: Unknown prepared statement handler given to MaxScale
* [MXS-5439](https://jira.mariadb.org/browse/MXS-5439) Backend connections with fail with EAGAIN
* [MXS-5438](https://jira.mariadb.org/browse/MXS-5438) Galeramon doesn't update GTID positions internally
* [MXS-5437](https://jira.mariadb.org/browse/MXS-5437) Failed authentication warnings do not mention lack of client-side SSL as the reason of the failure
* [MXS-5432](https://jira.mariadb.org/browse/MXS-5432) MaxScale 24.02.04 not closing DB Connections properly
* [MXS-5430](https://jira.mariadb.org/browse/MXS-5430) Authentication errors are sometimes not read if backend TLS is enabled
* [MXS-5419](https://jira.mariadb.org/browse/MXS-5419) Duration types that only take seconds return ms as units instead of s
* [MXS-5415](https://jira.mariadb.org/browse/MXS-5415) retry_failed_reads is not affected by delayed_retry_timeout
* [MXS-5409](https://jira.mariadb.org/browse/MXS-5409) list session in GUI shows wrong amount of sessions
* [MXS-5408](https://jira.mariadb.org/browse/MXS-5408) rebuild-server does not work with MariaDB 11.4
* [MXS-5404](https://jira.mariadb.org/browse/MXS-5404) The monitor journal file is not discarded aggressively enough.
* [MXS-5397](https://jira.mariadb.org/browse/MXS-5397) NVL and NVL2 are not detected as builtin functions outside of sql_mode=ORACLE
* [MXS-5395](https://jira.mariadb.org/browse/MXS-5395) Kafkacdc errors for wrong GTID positions are not clear
* [MXS-5382](https://jira.mariadb.org/browse/MXS-5382) Errors due to max_connections being exceeded are always fatal errors
* [MXS-5365](https://jira.mariadb.org/browse/MXS-5365) Binlogrouter cannot open compressed files
* [MXS-5340](https://jira.mariadb.org/browse/MXS-5340) ed25519 socket droped when no user_mapping_file
* [MXS-5314](https://jira.mariadb.org/browse/MXS-5314) Resultset table not fully expanded for inactive query tab

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
