# MariaDB MaxScale 24.02.4 Release Notes

Release 24.02.4 is a GA release.

This document describes the changes in release 24.02.4, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-24.02.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## External CVEs resolved.

* [CVE-2023-0437](https://www.cve.org/CVERecord?id=CVE-2023-0437) Fixed by [MXS-5400](https://jira.mariadb.org/browse/MXS-5400) Upgrade MongoC library to 1.27.5
* [CVE-2024-7553](https://www.cve.org/CVERecord?id=CVE-2024-7553) Fixed by [MXS-5400](https://jira.mariadb.org/browse/MXS-5400) Upgrade MongoC library to 1.27.5

## New Features

* [MXS-5214](https://jira.mariadb.org/browse/MXS-5214) servers_no_promotion should prevent a server from being promoted due to replication change
* [MXS-5177](https://jira.mariadb.org/browse/MXS-5177) Introduce a new core_file variable in MaxScale

## Bug fixes

* [MXS-5403](https://jira.mariadb.org/browse/MXS-5403) Debug assertion on very large binary protocol prepared statements
* [MXS-5402](https://jira.mariadb.org/browse/MXS-5402) Monitor connections do not check SSL certificate host even when ssl_verify_peer_host is enabled for the server
* [MXS-5398](https://jira.mariadb.org/browse/MXS-5398) Some log messages are not logged when session_trace is used
* [MXS-5394](https://jira.mariadb.org/browse/MXS-5394) Empty passwords are shown as non-empty if password encryption is enabled
* [MXS-5387](https://jira.mariadb.org/browse/MXS-5387) Crash in MariaDBParser::Helper::get_query_info()
* [MXS-5384](https://jira.mariadb.org/browse/MXS-5384) maxscale version 24 auth_all_servers not picking up users on replicas
* [MXS-5383](https://jira.mariadb.org/browse/MXS-5383) Alter table editor failed to assign table's collation value
* [MXS-5378](https://jira.mariadb.org/browse/MXS-5378) Nested listener parameters depend on protocol being defined
* [MXS-5377](https://jira.mariadb.org/browse/MXS-5377) Debug assert if backend fails during multi-packet query
* [MXS-5374](https://jira.mariadb.org/browse/MXS-5374) Kafkaimporter doesn't work with MariaDB 11
* [MXS-5372](https://jira.mariadb.org/browse/MXS-5372) timeout in kafkacdc is not a duration type
* [MXS-5366](https://jira.mariadb.org/browse/MXS-5366) Certain special characters in the maxscale user causes async-rebuild-server to fail
* [MXS-5364](https://jira.mariadb.org/browse/MXS-5364) Allow monitor ssh-parameters to be modified at runtime
* [MXS-5363](https://jira.mariadb.org/browse/MXS-5363) GDB stacktraces may hang
* [MXS-5361](https://jira.mariadb.org/browse/MXS-5361) Query editor: Column visibility filter shows wrong checkboxe values after search
* [MXS-5359](https://jira.mariadb.org/browse/MXS-5359) Transaction replay may deadlock with switchover
* [MXS-5357](https://jira.mariadb.org/browse/MXS-5357) Improve MariaDB Monitor documentation on auto_failover and auto_rejoin
* [MXS-5356](https://jira.mariadb.org/browse/MXS-5356) Readwritesplit master_reconnection is incompatible with GET_LOCK
* [MXS-5355](https://jira.mariadb.org/browse/MXS-5355) Switchover may cause two transaction replays to be started
* [MXS-5351](https://jira.mariadb.org/browse/MXS-5351) XA ROLLBACK is not treated as a rollback in MaxScale
* [MXS-5350](https://jira.mariadb.org/browse/MXS-5350) XA END is treated as the transaction commit instead of XA PREPARE
* [MXS-5344](https://jira.mariadb.org/browse/MXS-5344) Kafkaimporter constraint makes it difficult to use with kafkacdc
* [MXS-5343](https://jira.mariadb.org/browse/MXS-5343) Kafkacdc does not mention row-based replication as a requirement
* [MXS-5341](https://jira.mariadb.org/browse/MXS-5341) User account manager hangs on shutdown
* [MXS-5339](https://jira.mariadb.org/browse/MXS-5339) Slow servers may cause OOM situations if prepared statements are used
* [MXS-5338](https://jira.mariadb.org/browse/MXS-5338) Maxscale Admin Audit file should include ip address or host of calling session
* [MXS-5315](https://jira.mariadb.org/browse/MXS-5315) maxctrl destroy session takes only one ID as argument
* [MXS-5307](https://jira.mariadb.org/browse/MXS-5307) MaxScale kafkacdc logs "notice : Started replicating from [x.x.x.x]:3306 at GTID 'N-N-N' at every timeout/reconnection
* [MXS-5302](https://jira.mariadb.org/browse/MXS-5302) Prepared statements should never be removed from session command history
* [MXS-5298](https://jira.mariadb.org/browse/MXS-5298) Kafkacdc always reads last GTID from Kafka on startup
* [MXS-5296](https://jira.mariadb.org/browse/MXS-5296) New entity table is added without default collation
* [MXS-5273](https://jira.mariadb.org/browse/MXS-5273) The --config-check fails if /var/cache/maxscale cannot be read
* [MXS-5272](https://jira.mariadb.org/browse/MXS-5272) Monitor does not show broken external replication in "maxctrl list servers"
* [MXS-5269](https://jira.mariadb.org/browse/MXS-5269) Unexpected response after failed session command
* [MXS-5268](https://jira.mariadb.org/browse/MXS-5268) Read-only error during read-write transaction should trigger transaction replay
* [MXS-5264](https://jira.mariadb.org/browse/MXS-5264) MaxScale installs scripts with non-standard file permissions
* [MXS-5263](https://jira.mariadb.org/browse/MXS-5263) Valgrind reports read from uninitialized GWBUF for ccrfilter
* [MXS-5259](https://jira.mariadb.org/browse/MXS-5259) Retrying of reads on the current primary unnecessarily requires delayed_retry
* [MXS-5258](https://jira.mariadb.org/browse/MXS-5258) delayed_retry should not retry interrupted writes
* [MXS-5256](https://jira.mariadb.org/browse/MXS-5256) SET statements multiple values are not parsed correctly
* [MXS-5248](https://jira.mariadb.org/browse/MXS-5248) Debug assertion due to non-existent dcall ID
* [MXS-5247](https://jira.mariadb.org/browse/MXS-5247) Remove obsolete prelink script
* [MXS-5245](https://jira.mariadb.org/browse/MXS-5245) MaxCtrl does not accept dot notation for nested parameters
* [MXS-5239](https://jira.mariadb.org/browse/MXS-5239) Listener with ssl=false allows user accounts created with REQUIRE SSL to log in
* [MXS-5236](https://jira.mariadb.org/browse/MXS-5236) wsrep_desync behavior is undocumented
* [MXS-5229](https://jira.mariadb.org/browse/MXS-5229) Master Stickiness status not displayed correctly with use_priority
* [MXS-5218](https://jira.mariadb.org/browse/MXS-5218) Binlgorouter purge
* [MXS-5178](https://jira.mariadb.org/browse/MXS-5178) Replicas after maxscale binlog don't get updates
* [MXS-4933](https://jira.mariadb.org/browse/MXS-4933) Using  `targets=...` in `maxctrl create service` should be allowed

## Known Issues and Limitations

In the previous version of MaxScale, maxctrl was implemented as a JavaScript
script that was run using the node interpreter on the platform. That introduced
a dependency on node that earlier was not present. In this version of MaxScale,
maxctrl is again a native executable without that dependency.

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
