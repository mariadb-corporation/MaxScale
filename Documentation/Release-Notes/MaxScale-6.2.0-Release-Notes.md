# MariaDB MaxScale 6.2.0 Release Notes -- 2021-11-26

Release 6.2.0 is a GA release.

This document describes the changes in release 6.2.0, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-3813](https://jira.mariadb.org/browse/MXS-3813) Support PS direct execution in hintfilter
* [MXS-3771](https://jira.mariadb.org/browse/MXS-3771) Add row count to qlafilter
* [MXS-3755](https://jira.mariadb.org/browse/MXS-3755) Handle multiple replication sources on rejoin
* [MXS-3733](https://jira.mariadb.org/browse/MXS-3733) Add keytab filepath configuration option to GSSAPI authenticator
* [MXS-3701](https://jira.mariadb.org/browse/MXS-3701) Add canonical query form into qlafilter
* [MXS-3681](https://jira.mariadb.org/browse/MXS-3681) Refactor SQL GUI to support multiple  SQL connections
* [MXS-3680](https://jira.mariadb.org/browse/MXS-3680) Multiple SQL connections
* [MXS-3675](https://jira.mariadb.org/browse/MXS-3675) History/favorite queries
* [MXS-3659](https://jira.mariadb.org/browse/MXS-3659) Show Slave Status in GUI
* [MXS-3645](https://jira.mariadb.org/browse/MXS-3645) Transaction Performance Monitoring Filter functionality moved to Qlafilter
* [MXS-3639](https://jira.mariadb.org/browse/MXS-3639) Show Stored procedures and Triggers  in schema tree
* [MXS-3638](https://jira.mariadb.org/browse/MXS-3638) Multi-tab query editor
* [MXS-3636](https://jira.mariadb.org/browse/MXS-3636) Filter result by specific column
* [MXS-3635](https://jira.mariadb.org/browse/MXS-3635) Quicker access to "Place Schema in Editor"
* [MXS-3634](https://jira.mariadb.org/browse/MXS-3634) Add DDL editor
* [MXS-3632](https://jira.mariadb.org/browse/MXS-3632) Add right click context menu to schema tree
* [MXS-3613](https://jira.mariadb.org/browse/MXS-3613) Support PS with metadata skip (i.e. MARIADB_CLIENT_CACHE_METADATA)
* [MXS-3526](https://jira.mariadb.org/browse/MXS-3526) GSSAPI authenticator supports only one principal and only the default location for the keytab
* [MXS-3475](https://jira.mariadb.org/browse/MXS-3475) Extend PAM support to include Group Mapping
* [MXS-3453](https://jira.mariadb.org/browse/MXS-3453) Add counter for failed authentication attempts
* [MXS-3363](https://jira.mariadb.org/browse/MXS-3363) Make tee filter also syncronously
* [MXS-3281](https://jira.mariadb.org/browse/MXS-3281) r/w split slave_selection_criteria should have none
* [MXS-3037](https://jira.mariadb.org/browse/MXS-3037) show detail processlist like at mariadb
* [MXS-2074](https://jira.mariadb.org/browse/MXS-2074) Handle prepared statements in NamedServerFilter
* [MXS-1892](https://jira.mariadb.org/browse/MXS-1892) Support deprecate eof

## Bug fixes

* [MXS-3883](https://jira.mariadb.org/browse/MXS-3883) nosqlprotocol parameters are not serialized correctly
* [MXS-3881](https://jira.mariadb.org/browse/MXS-3881) Maxscale killing connection to backend node when load data infile is run with transaction replay
* [MXS-3880](https://jira.mariadb.org/browse/MXS-3880) Bias value for open connections is too large
* [MXS-3878](https://jira.mariadb.org/browse/MXS-3878) The create server command doesn't allow custom parameters
* [MXS-3876](https://jira.mariadb.org/browse/MXS-3876) sys schema not ignored by schemarouter
* [MXS-3857](https://jira.mariadb.org/browse/MXS-3857) Pinloki initial gtid scan incorrectly reads entire files
* [MXS-3849](https://jira.mariadb.org/browse/MXS-3849) Unable to configure nested parameters via MaxCtrl
* [MXS-3847](https://jira.mariadb.org/browse/MXS-3847) Node hostname is not escaped properly
* [MXS-3846](https://jira.mariadb.org/browse/MXS-3846) debug=enable-statement-logging doesn't work with mxq::MariaDB
* [MXS-3845](https://jira.mariadb.org/browse/MXS-3845) Sending binlog events is inefficient
* [MXS-3838](https://jira.mariadb.org/browse/MXS-3838) Add reconnect functionality to the GUI
* [MXS-3836](https://jira.mariadb.org/browse/MXS-3836) refresh_databases does nothing
* [MXS-3835](https://jira.mariadb.org/browse/MXS-3835) Timeout for connection dialog in the GUI is not parsed as number
* [MXS-3834](https://jira.mariadb.org/browse/MXS-3834) SQL API has no reconnect functionality
* [MXS-3833](https://jira.mariadb.org/browse/MXS-3833) Query editor timeout is too aggressive
* [MXS-3824](https://jira.mariadb.org/browse/MXS-3824) Allow symbolic link for path to directory /usr/share/maxscale/gui
* [MXS-3815](https://jira.mariadb.org/browse/MXS-3815) maxscale crash
* [MXS-3814](https://jira.mariadb.org/browse/MXS-3814) maxscale rpl_state is empty
* [MXS-3809](https://jira.mariadb.org/browse/MXS-3809) When MariaDBMonitor acquires lock majority, the log message gives the impression that auto_failover is enabled even when it is not configured
* [MXS-3800](https://jira.mariadb.org/browse/MXS-3800) Not enough information in server state change messages
* [MXS-3793](https://jira.mariadb.org/browse/MXS-3793) Race condition in GCUpdater shutdown
* [MXS-3778](https://jira.mariadb.org/browse/MXS-3778) MaxScale crashes when using Cache filter
* [MXS-3777](https://jira.mariadb.org/browse/MXS-3777) COMMIT in prepared statement causes warnings
* [MXS-3772](https://jira.mariadb.org/browse/MXS-3772) Qlafilter message timing is inconsistent
* [MXS-3770](https://jira.mariadb.org/browse/MXS-3770) Bundle Connector-C plugins with MaxScale
* [MXS-3736](https://jira.mariadb.org/browse/MXS-3736) Crash with kafkaimporter and no servers
* [MXS-3730](https://jira.mariadb.org/browse/MXS-3730) idle_session_pool_time=0s doesn't immediately pool idle connections
* [MXS-3720](https://jira.mariadb.org/browse/MXS-3720) idle_session_pool_time should support prepared statements
* [MXS-3717](https://jira.mariadb.org/browse/MXS-3717) Crash on object type change with config_sync_cluster
* [MXS-3711](https://jira.mariadb.org/browse/MXS-3711) Qlafilter cannot be modified at runtime
* [MXS-3710](https://jira.mariadb.org/browse/MXS-3710) Namedserverfilter cannot be modified at runtime
* [MXS-3709](https://jira.mariadb.org/browse/MXS-3709) Masking filter cannot be modified at runtime
* [MXS-3690](https://jira.mariadb.org/browse/MXS-3690) MaxCtrl parameter format is inconsistent
* [MXS-3689](https://jira.mariadb.org/browse/MXS-3689) Protocol module configurations are not persisted
* [MXS-3688](https://jira.mariadb.org/browse/MXS-3688) MaxCtrl doesn't support listener module parameters
* [MXS-3687](https://jira.mariadb.org/browse/MXS-3687) Lack of nested parameters is not detected
* [MXS-3686](https://jira.mariadb.org/browse/MXS-3686) Object names with characters outside of the ASCII range are not rejected
* [MXS-3685](https://jira.mariadb.org/browse/MXS-3685) nosqlprotocol doesn't start TLS session
* [MXS-3672](https://jira.mariadb.org/browse/MXS-3672) MaxCtrl output is not very readable
* [MXS-3630](https://jira.mariadb.org/browse/MXS-3630) Update user accounts when backend login fails
* [MXS-3618](https://jira.mariadb.org/browse/MXS-3618) config_sync_cluster change detection is inadequate
* [MXS-3594](https://jira.mariadb.org/browse/MXS-3594) Filters can be altered but no filter declares parameters as alterable
* [MXS-3580](https://jira.mariadb.org/browse/MXS-3580) Avrorouter should store full GTID coordinates
* [MXS-3514](https://jira.mariadb.org/browse/MXS-3514) Speed up special query parsing (pcre2)
* [MXS-3498](https://jira.mariadb.org/browse/MXS-3498) Improve Multistatement detect performance
* [MXS-3458](https://jira.mariadb.org/browse/MXS-3458) Execution of PS fails when strict_sp_calls is enabled
* [MXS-3359](https://jira.mariadb.org/browse/MXS-3359) QLA filter append= should default to true, at least when using log_type=unified
* [MXS-3353](https://jira.mariadb.org/browse/MXS-3353) Tee filter loses statements if branch target is slower
* [MXS-3308](https://jira.mariadb.org/browse/MXS-3308) Passing options in interactive mode returns an empty error
* [MXS-2992](https://jira.mariadb.org/browse/MXS-2992) ALTER TABLE statements not working with masking filter

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
