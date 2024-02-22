# Changelog

## MariaDB MaxScale 24.02

* The default values of some readwritesplit parameters have been updated. The
  new default values are:

    - `master_reconnection=true`
    - `master_failure_mode=fail_on_write`
    - `strict_tmp_tables=true`
    - `transaction_replay_timeout=30s`

* MariaDB-Monitor now requires MariaDB Server 10.4 or newer for failover/
  switchover. Server 10.3 is end of life.

* Server setting `private_address` added. Used for detecting and setting up
  replication. See
  [MariaDB Monitor documentation](Monitors/MariaDB-Monitor.md#private_address)
  for more information.

* MariaDB-Monitor allows customization of some Mariabackup settings. See
  [mariabackup_use_memory](Monitors/MariaDB-Monitor.md#mariabackup_use_memory)
  and [mariabackup_parallel](Monitors/MariaDB-Monitor.md#mariabackup_parallel)
  for more information.

* Pam authentication now always checks account status. Previously, this check
  was not performed if MaxScale was configured for username mapping
  (`authenticator_options=pam_backend_mapping=mariadb`). This means that if
  username mapping is configured in the OS pam service config, the final
  username must be a valid user. This is similar to MariaDB Server behavior.

* Support for the legacy SysV and Upstart system managers has been removed. In
  practice this change will not affect anything as all supported operating
  systems use SystemD as the system manager and MaxScale has long preferred it
  over the legacy systems.

* The output of `maxctrl show service` now includes the statistics for
  the services.

* The `max_sescmd_history_length` and `avg_sescmd_history_length` statistics in
  readwritesplit were moved into the core as service statistics. The session
  command history was moved into the MaxScale core in MaxScale 6 but the
  statistics were not updated to match this.

* Several redundant schemarouter statistics have been either replaced by
  statistics that are found in the general service statistics output or have
  been removed if they were irrelevant.

    - `longest_sescmd_chain` replaced by `max_sescmd_history_length`.
    - `queries` replaced with `routed_packets`.
    - `times_sescmd_limit_exceeded` has been removed.
    - `sescmd_percentage` has been removed.
    - `longest_session` replaced with `max_session_lifetime`.
    - `average_session` replaced with `avg_session_lifetime`.
    - `shortest_session` has been removed.

* `admin_readwrite_hosts` and `admin_readonly_hosts` added. These settings limit
  the allowed source addresses for admin (REST-API) connections. See
  [here](Getting-Started/Configuration-Guide.md#admin_readwrite_hosts)
  for more information.

* The `alter` commands in MaxCtrl now allow modifications to the
  `targets`, `servers`, `filters` and `cluster` parameters for monitors
  and services. This makes it easier to define the exact set of servers
  that is to be used by a monitor or a service.

* The [causal_reads](Routers/ReadWriteSplit.md#causal_reads) feature no longer
  requires modifying `session_track_system_variables` on
  the server. MaxScale adds `last_gtid` to the variable automatically
  for each session. Clients should not modify it manually.

* The [qlafilter](Filters/Query-Log-All-Filter.md) now logs the execution of the
  binary protocol statements as text. This makes it so that for all execution of
  SQL, the `query` value in `log_data` will produce output.

* The `auth_all_servers` parameter has been deprecated as it is now
  obsolete: the functionality that it enabled is automatically enabled
  by the schemarouter whenever it is used.

For more details, please refer to:

* [MariaDB MaxScale 24.02.0 Release Notes](Release-Notes/MaxScale-24.02.0-Release-Notes.md)

## MariaDB MaxScale 23.08

* The global setting `skip_permission_checks` has been deprecated and is
  ignored. Monitors start regardless of monitor user permissions.

* The uppercase versions of the `slave_selection_criteria` parameter in
  readwritesplit have been deprecated. All runtime modifications to the
  parameters are now saved using the lowercase versions.

* By default, readwritesplit will not replay transactions that are about to
  commit when `transaction_replay` is enabled. To retain the old behavior where
  transactions were always replayed, disable `transaction_replay_safe_commit`.

* Readwritesplit will now retry queries with partially returned results if they
  are done inside a transaction and `transaction_replay` is enabled.

* `prune_sescmd_history` now also performs history simplification by removing
  redundant executions of the same statement. This will reduce memory usage in
  the case when history repeats a small cycle of commands. One example of this
  is connection pools that prepare the connection with a small set of commands
  like `SET NAMES` and `SET SQL_MODE`.

* Common configuration settings can now be specified in a separate section,
  to be included by other sections.

* When changing the service password, the previous password will be retained
  and continue to be used if the new does not work.

* Embedded newlines are removed from logged messages.

* Schemarouter database map caches can be cleared with `maxctrl call command
  schemarouter clear <service>` where `<service>` is a service that uses the
  schemarouter.

* Schemarouter now allows stale cache entries to be used while the database map
  cache is being updated. By default the entries are usable for 150 seconds
  after they have gone stale. This limit can be configured with the
  `max_staleness` parameter.

* Added switchover-force command to MariaDB Monitor. This command performs a
  switchover even if primary server is unresponsive.

* Switchover now uses a longer command timeout on the old master. This should
  remove the need for adjusting monitor setting `backend_read_timeout` to get
  switchover to work.

* The NoSQL protocol now provides internal caching.

* Additional metadata is sent in the connection handshake. For more information,
  refer to the
  [parameter](Getting-Started/Configuration-Guide.md#connection_metadata)
  documentation.

* The `connection_timeout` parameter was renamed to `wait_timeout` to make the
  naming of the parameters the same in MaxScale and MariaDB server. The old
  `connection_timeout` parameter is now an alias to `wait_timeout` and its use
  has been deprecated. If used, a warning will be logged that the parameter is
  deprecated.

* The `strip_db_esc` parameter has been deprecated. The default behavior of
  stripping the escape characters is in all known cases the correct thing to
  do. Only broken legacy versions where the grants would be returned without
  backslash escaping would require this parameter to work.

* The Xpand monitor is now region aware.

* Passthrough authentication mode added to MariaDBAuth-module. See
  [authenticator documentation](Authenticators/MySQL-Authenticator.md#clear_pw_passthrough) for more
  information.

* Added `pam_mode=suid` option to PamAuth module. This option separates the
  pam system library calls to a separate executable. See
  [authenticator documentation](Authenticators/PAM-Authenticator.md#pam_mode)
  for more information. *test_pam_login* tool updated to support this mode.

For more details, please refer to:

* [MariaDB MaxScale 23.08.4 Release Notes](Release-Notes/MaxScale-23.08.4-Release-Notes.md)
* [MariaDB MaxScale 23.08.3 Release Notes](Release-Notes/MaxScale-23.08.3-Release-Notes.md)
* [MariaDB MaxScale 23.08.2 Release Notes](Release-Notes/MaxScale-23.08.2-Release-Notes.md)
* [MariaDB MaxScale 23.08.1 Release Notes](Release-Notes/MaxScale-23.08.1-Release-Notes.md)
* [MariaDB MaxScale 23.08.0 Release Notes](Release-Notes/MaxScale-23.08.0-Release-Notes.md)

## MariaDB MaxScale 23.02

* A transition from the traditional _master/slave_ terminology to the
  _primary/replica_ terminology has been started. In the documentation
  and in the logging the transition has been made, but in configuration
  settings and command output the traditional terminology is still used.
  Conceptually,  _master/slave_ and _primary/replica_ are completely
  interchangeable.

* MariaDB Monitor now preserves the MASTER_USE_GTID-setting of a replica when
  redirecting one during switchover and failover. When starting a new
  replication connection on a previous replica, Slave_Pos is used. When starting
  a new replication connection on a previous primary, Current_Pos is used.

* Added `replication_custom_options`-setting to both MariaDB Monitor and server.
  This setting enables e.g. setting SSL certificates for replication
  connections. See [MariaDB Monitor documentation](Monitors/MariaDB-Monitor.md#replication_custom_options)
  for more information.

* Create-Backup and Restore-From-Backup commands added to MariaDBMonitor.

* The `csmon` and `auroramon` monitors that were deprecated in 22.08.2
  have been removed.

* The obsolete `maxctrl drain` command has been removed. Use `maxctrl set server
  <name> drain` to use the built-in draining mechanism.

* The `maxctrl cluster` commands have been removed. Use the built-in
  [configuration synchronization](Getting-Started/Configuration-Guide.md#configuration-synchronization)
  to synchronize the configurations of multiple MaxScale instances.

* The REST-API is now supports ODBC-type connections in the `/sql`
  endpoints. For more information, refer to the SQL resource
  [documentation](./REST-API/Resources-SQL.md#open-sql-connection-to-server).

* The REST-API query endpoint now supports asynchronous query execution
  using the `async=true` option.

* The new `/sql/:id/etl` endpoints allow ETL operations to be done from
  ODBC data sources into MariaDB servers. For more information on the
  new API functions, refer to the SQL resource
  [documentation](./REST-API/Resources-SQL.md#prepare-etl-operation).

* The number of routing threads MaxScale uses can now be changed at runtime.

* The Audit-Log allows all REST-API calls to be logged.

* MaxScale now supports Xpand parallel replication streams that go through
  MaxScale.

* Authentication can now be enabled when Redis is used as the cache storage.
* SSL/TLS can now be used in the communication between MaxScale and Redis
  when Redis is used as the storage for the cache.
* Some Cache configuration parameters, most notable the rules, can now be
  changed at runtime.
* Support for inbound proxy protocol added.
* Ed25519Auth-plugin added. The plugin enables authentication with the
  MariaDB Server ed25519 authentication plugin.
* MaxGUI Query Editor has been renamed to "Workspace," and its internal storage
  key has been modified. As a result, the query history, snippets, and user
  preferences settings are restored to their default values.

* The redundant options for `create server` and `create listener` that were
  deprecated in 22.08 have been removed in MaxScale 23.02. The use of explicit
  options has been superseded by the use of `key=value` parameters in all
  `create` commands.

For more details, please refer to:

* [MariaDB MaxScale 23.02.8 Release Notes](Release-Notes/MaxScale-23.02.8-Release-Notes.md)
* [MariaDB MaxScale 23.02.7 Release Notes](Release-Notes/MaxScale-23.02.7-Release-Notes.md)
* [MariaDB MaxScale 23.02.6 Release Notes](Release-Notes/MaxScale-23.02.6-Release-Notes.md)
* [MariaDB MaxScale 23.02.5 Release Notes](Release-Notes/MaxScale-23.02.5-Release-Notes.md)
* [MariaDB MaxScale 23.02.4 Release Notes](Release-Notes/MaxScale-23.02.4-Release-Notes.md)
* [MariaDB MaxScale 23.02.3 Release Notes](Release-Notes/MaxScale-23.02.3-Release-Notes.md)
* [MariaDB MaxScale 23.02.2 Release Notes](Release-Notes/MaxScale-23.02.2-Release-Notes.md)
* [MariaDB MaxScale 23.02.1 Release Notes](Release-Notes/MaxScale-23.02.1-Release-Notes.md)
* [MariaDB MaxScale 23.02.0 Release Notes](Release-Notes/MaxScale-23.02.0-Release-Notes.md)

## MariaDB MaxScale 22.08

* Sessions can now be restarted so that added server are taken into use.
* Sessions can now be killed using maxctrl.
* MariaDBMonitor can use Mariabackup to clone the contents of a server.
* MariaDBMonitor can issue ColumnStore commands similar to CSMon
* MariaDBMonitor settings `ignore_external_masters`, `detect_replication_lag`
  `detect_standalone_master`, `detect_stale_master` and `detect_stale_slave`
  have been removed. The first two were ineffective, the latter three are
  replaced by `master_conditions` and `slave_conditions`.
* A new Query Rewrite filter, using which queries can be rewritten based
  upon a template, has been added.
* MaxScale no longer logs to both the SystemD journal and MaxScale log by
  default: the default value of `syslog` was changed from `true` to `false` to
  reduce the amount of redundant log messages that are logged. To retain the old
  behavior of logging to both MaxScale's own files and to the SystemD journal,
  add `syslog=true` under the `[maxscale]` section.
* The `dbfwfilter` module that was deprecated in version 6 has now been removed.
* MaxGUI Query Editor has changed the type of browser storage from local storage
  to IndexedDB. As the result, query history, favorite, and configuration are reset.
  Apart from that, query favorite was renamed to query snippets allowing to quickly
  insert the query to the editor by typing its prefix. See MaxGUI tutorial
  [Using-MaxGUI-Tutorial](Tutorials/Using-MaxGUI-Tutorial.md)
* The Xpand monitor now handles group change explicitly.
* The `Maintenance|Drain` state of a server is now synchronized between multiple
  MaxScale instances if configuration synchronization  is enabled.
* Causal reads now supported in a multi-MaxScale setup.
* The `csmon` and `auroramon` monitors are deprecated in 22.08.2 and will be
  removed in 23.02.0.

For more details, please refer to:

* [MariaDB MaxScale 22.08.11 Release Notes](Release-Notes/MaxScale-22.08.11-Release-Notes.md)
* [MariaDB MaxScale 22.08.10 Release Notes](Release-Notes/MaxScale-22.08.10-Release-Notes.md)
* [MariaDB MaxScale 22.08.9 Release Notes](Release-Notes/MaxScale-22.08.9-Release-Notes.md)
* [MariaDB MaxScale 22.08.8 Release Notes](Release-Notes/MaxScale-22.08.8-Release-Notes.md)
* [MariaDB MaxScale 22.08.7 Release Notes](Release-Notes/MaxScale-22.08.7-Release-Notes.md)
* [MariaDB MaxScale 22.08.6 Release Notes](Release-Notes/MaxScale-22.08.6-Release-Notes.md)
* [MariaDB MaxScale 22.08.5 Release Notes](Release-Notes/MaxScale-22.08.5-Release-Notes.md)
* [MariaDB MaxScale 22.08.4 Release Notes](Release-Notes/MaxScale-22.08.4-Release-Notes.md)
* [MariaDB MaxScale 22.08.3 Release Notes](Release-Notes/MaxScale-22.08.3-Release-Notes.md)
* [MariaDB MaxScale 22.08.2 Release Notes](Release-Notes/MaxScale-22.08.2-Release-Notes.md)
* [MariaDB MaxScale 22.08.1 Release Notes](Release-Notes/MaxScale-22.08.1-Release-Notes.md)
* [MariaDB MaxScale 22.08.0 Release Notes](Release-Notes/MaxScale-22.08.0-Release-Notes.md)

## MariaDB MaxScale 6.4

* Added `multiplex_timeout`. It sets the time a session can wait
 for a backend connection to become available when using connection sharing
 (i.e. `idle_session_pool_time`).

For more details, please refer to:

* [MariaDB MaxScale 6.4.14 Release Notes](Release-Notes/MaxScale-6.4.14-Release-Notes.md)
* [MariaDB MaxScale 6.4.13 Release Notes](Release-Notes/MaxScale-6.4.13-Release-Notes.md)
* [MariaDB MaxScale 6.4.12 Release Notes](Release-Notes/MaxScale-6.4.12-Release-Notes.md)
* [MariaDB MaxScale 6.4.11 Release Notes](Release-Notes/MaxScale-6.4.11-Release-Notes.md)
* [MariaDB MaxScale 6.4.10 Release Notes](Release-Notes/MaxScale-6.4.10-Release-Notes.md)
* [MariaDB MaxScale 6.4.9 Release Notes](Release-Notes/MaxScale-6.4.9-Release-Notes.md)
* [MariaDB MaxScale 6.4.8 Release Notes](Release-Notes/MaxScale-6.4.8-Release-Notes.md)
* [MariaDB MaxScale 6.4.7 Release Notes](Release-Notes/MaxScale-6.4.7-Release-Notes.md)
* [MariaDB MaxScale 6.4.6 Release Notes](Release-Notes/MaxScale-6.4.6-Release-Notes.md)
* [MariaDB MaxScale 6.4.5 Release Notes](Release-Notes/MaxScale-6.4.5-Release-Notes.md)
* [MariaDB MaxScale 6.4.4 Release Notes](Release-Notes/MaxScale-6.4.4-Release-Notes.md)
* [MariaDB MaxScale 6.4.3 Release Notes](Release-Notes/MaxScale-6.4.3-Release-Notes.md)
* [MariaDB MaxScale 6.4.2 Release Notes](Release-Notes/MaxScale-6.4.2-Release-Notes.md)
* [MariaDB MaxScale 6.4.1 Release Notes](Release-Notes/MaxScale-6.4.1-Release-Notes.md)
* [MariaDB MaxScale 6.4.0 Release Notes](Release-Notes/MaxScale-6.4.0-Release-Notes.md)

## MariaDB MaxScale 6.3

* Nosqlprotocol now supports both SSL and authentication.
* A large number of client connections can now share a smaller number of
  backend connections.
* MaxGUI now shows which MaxScale node is in control when cooperative monitoring
  is enabled.
* Filtering can now be used in the KafkaCDC router.
* The user is now informed if runtime changes are made to configuration objects
  that are defined in static configuration files.

For more details, please refer to:

* [MariaDB MaxScale 6.3.1 Release Notes](Release-Notes/MaxScale-6.3.1-Release-Notes.md)
* [MariaDB MaxScale 6.3.0 Release Notes](Release-Notes/MaxScale-6.3.0-Release-Notes.md)

## MariaDB MaxScale 6.2

* Significant improvements and feature additions to MaxGUI
* Significant improvements to nosqlprotocol
* Transaction Performance Monitoring Filter functionality moved to Qlafilter
* Most filters can now be reconfigured at runtime
* Synchronous mode for the Tee filter
* New `list queries` command for MaxCtrl that lists all active queries
* MaxScale can read client user accounts from a file and map them to backend
  users. See service setting
  [user_accounts_file](Getting-Started/Configuration-Guide.md#user_accounts_file)
  and listener setting
  [user_mapping_file](Getting-Started/Configuration-Guide.md#user_mapping_file)
  for more information.

For more details, please refer to:

* [MariaDB MaxScale 6.2.4 Release Notes](Release-Notes/MaxScale-6.2.4-Release-Notes.md)
* [MariaDB MaxScale 6.2.3 Release Notes](Release-Notes/MaxScale-6.2.3-Release-Notes.md)
* [MariaDB MaxScale 6.2.2 Release Notes](Release-Notes/MaxScale-6.2.2-Release-Notes.md)
* [MariaDB MaxScale 6.2.1 Release Notes](Release-Notes/MaxScale-6.2.1-Release-Notes.md)
* [MariaDB MaxScale 6.2.0 Release Notes](Release-Notes/MaxScale-6.2.0-Release-Notes.md)

## MariaDB MaxScale 6.1

* The versioning scheme has changed; earlier this would have been version 2.6.
* A `nosqlprotocol` protocol module that implements the MongoDB® wire protocol
  has been introduced.
* The Columnstore monitor is now exclusively intended for Columnstore version 1.5.
* The Columnstore monitor can now automatically adapt to changes in the cluster
  configuration.
* The Database Firewall filter has been deprecated.
* If *extra_port* is defined for a server, it's used by default for monitor and
user account manager connections. Normal port is used if the extra-port
connection fails due to too low *extra_max_connections*-setting on the backend.
* The deprecated `required` and `disabled` values for the `ssl` parameter have been removed.
* Backend connection multiplexing added. See
[idle_session_pool_time](Getting-Started/Configuration-Guide.md#idle_session_pool_time)
for more information.
* Defaults for `maxctrl` can now be specified in the file `~/maxctrl.cnf`
* PAM Authenticator can map PAM users to MariaDB users.
* MariaDB-Monitor can launch monitor script when slave server exceeds replication
lag limit (`script_max_replication_lag`).
* MariaDB-Monitor can disable *read_only* on master server
(`enforce_writable_master`).
* A graphical user interface SQL queries tool for writing, running SQL queries and visualizing
the results has been introduced.

For more details, please refer to:

* [MariaDB MaxScale 6.1.4 Release Notes](Release-Notes/MaxScale-6.1.4-Release-Notes.md)
* [MariaDB MaxScale 6.1.3 Release Notes](Release-Notes/MaxScale-6.1.3-Release-Notes.md)
* [MariaDB MaxScale 6.1.2 Release Notes](Release-Notes/MaxScale-6.1.2-Release-Notes.md)
* [MariaDB MaxScale 6.1.1 Release Notes](Release-Notes/MaxScale-6.1.1-Release-Notes.md)
* [MariaDB MaxScale 6.1.0 Release Notes](Release-Notes/MaxScale-6.1.0-Release-Notes.md)
* [MariaDB MaxScale 6.0.0 Release Notes](Release-Notes/MaxScale-6.0.0-Release-Notes.md)

## MariaDB MaxScale 2.5

* MaxAdmin has been removed.
* MaxGUI, a new browser based tool for configuring and managing
  MaxScale is introduced.
* MaxInfo-router and the related httpd-protocol have been removed.
* Server weights have been removed.
* Services can now directly route to other services with the help of the
  [`target`](Getting-Started/Configuration-Guide.md#target) parameter.
* Server parameters *protocol* and *authenticator* have been deprecated. Any
  definitions are ignored.
* Listeners support multiple authenticators.
* The replication lag of a slave server must now be less than
  [`max_slave_replication_lag`](Routers/ReadWriteSplit.md#max_slave_replication_lag)
  whereas in older versions the replication lag had to be less than or
  equal to the configured limit.
* The global settings *auth_read_timeout* and *auth_write_timeout* have been
  deprecated. Any definitions are ignored.
* The Columnstore monitor is now capable of monitoring Columnstore 1.5 in
  addition to 1.0 and 1.2.
* MariaDB-Monitor supports cooperative monitoring. See
  [cooperative monitoring](Monitors/MariaDB-Monitor.md#cooperative-monitoring)
  for more information.
* The MaxScale cache can now be shared between two MaxScale instances,
  in which case either memcached or Redis can be used as cache storage.
  Further, the cache can now also perform table level invalidations
  and be specific to a particular user.
* A completely new binlog router implementation.
* New routers, [mirror](Routers/Mirror.md) and [kafkacdc](Routers/KafkaCDC.md).
* Service-to-service routing is now possible with the `targets` parameter.
* TLS CRL and peer host verification support.
* Multiple modes of operation for `causal_reads`.

For more details, please refer to:

* [MariaDB MaxScale 2.5.29 Release Notes](Release-Notes/MaxScale-2.5.29-Release-Notes.md)
* [MariaDB MaxScale 2.5.28 Release Notes](Release-Notes/MaxScale-2.5.28-Release-Notes.md)
* [MariaDB MaxScale 2.5.27 Release Notes](Release-Notes/MaxScale-2.5.27-Release-Notes.md)
* [MariaDB MaxScale 2.5.26 Release Notes](Release-Notes/MaxScale-2.5.26-Release-Notes.md)
* [MariaDB MaxScale 2.5.25 Release Notes](Release-Notes/MaxScale-2.5.25-Release-Notes.md)
* [MariaDB MaxScale 2.5.24 Release Notes](Release-Notes/MaxScale-2.5.24-Release-Notes.md)
* [MariaDB MaxScale 2.5.23 Release Notes](Release-Notes/MaxScale-2.5.23-Release-Notes.md)
* [MariaDB MaxScale 2.5.22 Release Notes](Release-Notes/MaxScale-2.5.22-Release-Notes.md)
* [MariaDB MaxScale 2.5.21 Release Notes](Release-Notes/MaxScale-2.5.21-Release-Notes.md)
* [MariaDB MaxScale 2.5.20 Release Notes](Release-Notes/MaxScale-2.5.20-Release-Notes.md)
* [MariaDB MaxScale 2.5.19 Release Notes](Release-Notes/MaxScale-2.5.19-Release-Notes.md)
* [MariaDB MaxScale 2.5.18 Release Notes](Release-Notes/MaxScale-2.5.18-Release-Notes.md)
* [MariaDB MaxScale 2.5.17 Release Notes](Release-Notes/MaxScale-2.5.17-Release-Notes.md)
* [MariaDB MaxScale 2.5.16 Release Notes](Release-Notes/MaxScale-2.5.16-Release-Notes.md)
* [MariaDB MaxScale 2.5.15 Release Notes](Release-Notes/MaxScale-2.5.15-Release-Notes.md)
* [MariaDB MaxScale 2.5.14 Release Notes](Release-Notes/MaxScale-2.5.14-Release-Notes.md)
* [MariaDB MaxScale 2.5.13 Release Notes](Release-Notes/MaxScale-2.5.13-Release-Notes.md)
* [MariaDB MaxScale 2.5.12 Release Notes](Release-Notes/MaxScale-2.5.12-Release-Notes.md)
* [MariaDB MaxScale 2.5.11 Release Notes](Release-Notes/MaxScale-2.5.11-Release-Notes.md)
* [MariaDB MaxScale 2.5.10 Release Notes](Release-Notes/MaxScale-2.5.10-Release-Notes.md)
* [MariaDB MaxScale 2.5.9 Release Notes](Release-Notes/MaxScale-2.5.9-Release-Notes.md)
* [MariaDB MaxScale 2.5.8 Release Notes](Release-Notes/MaxScale-2.5.8-Release-Notes.md)
* [MariaDB MaxScale 2.5.7 Release Notes](Release-Notes/MaxScale-2.5.7-Release-Notes.md)
* [MariaDB MaxScale 2.5.6 Release Notes](Release-Notes/MaxScale-2.5.6-Release-Notes.md)
* [MariaDB MaxScale 2.5.5 Release Notes](Release-Notes/MaxScale-2.5.5-Release-Notes.md)
* [MariaDB MaxScale 2.5.4 Release Notes](Release-Notes/MaxScale-2.5.4-Release-Notes.md)
* [MariaDB MaxScale 2.5.3 Release Notes](Release-Notes/MaxScale-2.5.3-Release-Notes.md)
* [MariaDB MaxScale 2.5.2 Release Notes](Release-Notes/MaxScale-2.5.2-Release-Notes.md)
* [MariaDB MaxScale 2.5.1 Release Notes](Release-Notes/MaxScale-2.5.1-Release-Notes.md)
* [MariaDB MaxScale 2.5.0 Release Notes](Release-Notes/MaxScale-2.5.0-Release-Notes.md)

## MariaDB MaxScale 2.4

* A Clustrix specific monitor has been added.
* A new router, Smart Router, capable of routing a query to different
  backends depending on the characteristics of the query has been added.
* Transaction replaying is now performed also in conjunction with server
  initiated transaction rollbacks.
* Names starting with `@@` are reserved for use by MaxScale.
* Names can no longer contain whitespace.
* Servers can now be drained.
* The servers of a service can now be defined using a monitor.
* Durations can now be specified as hours, minutes, seconds or milliseconds.
* MaxCtrl commands `list sessions`, `show sessions` and `show session <id>`
  support reverse DNS lookup of client addresses. The conversion is activated
  by adding the `--rdns`-option to the command.
* The following MariaDB-Monitor settings have been removed and cause a startup error
  if defined: `mysql51_replication`, `multimaster` and `allow_cluster_recovery`. The
  setting `detect_replication_lag` is deprecated and ignored.
* `enforce_simple_topology`-setting added to MariaDB-Monitor.
* The mqfilter has been deprecated.

For more details, please refer to:

* [MariaDB MaxScale 2.4.19 Release Notes](Release-Notes/MaxScale-2.4.19-Release-Notes.md)
* [MariaDB MaxScale 2.4.18 Release Notes](Release-Notes/MaxScale-2.4.18-Release-Notes.md)
* [MariaDB MaxScale 2.4.17 Release Notes](Release-Notes/MaxScale-2.4.17-Release-Notes.md)
* [MariaDB MaxScale 2.4.16 Release Notes](Release-Notes/MaxScale-2.4.16-Release-Notes.md)
* [MariaDB MaxScale 2.4.15 Release Notes](Release-Notes/MaxScale-2.4.15-Release-Notes.md)
* [MariaDB MaxScale 2.4.14 Release Notes](Release-Notes/MaxScale-2.4.14-Release-Notes.md)
* [MariaDB MaxScale 2.4.13 Release Notes](Release-Notes/MaxScale-2.4.13-Release-Notes.md)
* [MariaDB MaxScale 2.4.12 Release Notes](Release-Notes/MaxScale-2.4.12-Release-Notes.md)
* [MariaDB MaxScale 2.4.11 Release Notes](Release-Notes/MaxScale-2.4.11-Release-Notes.md)
* [MariaDB MaxScale 2.4.10 Release Notes](Release-Notes/MaxScale-2.4.10-Release-Notes.md)
* [MariaDB MaxScale 2.4.9 Release Notes](Release-Notes/MaxScale-2.4.9-Release-Notes.md)
* [MariaDB MaxScale 2.4.8 Release Notes](Release-Notes/MaxScale-2.4.8-Release-Notes.md)
* [MariaDB MaxScale 2.4.7 Release Notes](Release-Notes/MaxScale-2.4.7-Release-Notes.md)
* [MariaDB MaxScale 2.4.6 Release Notes](Release-Notes/MaxScale-2.4.6-Release-Notes.md)
* [MariaDB MaxScale 2.4.5 Release Notes](Release-Notes/MaxScale-2.4.5-Release-Notes.md)
* [MariaDB MaxScale 2.4.4 Release Notes](Release-Notes/MaxScale-2.4.4-Release-Notes.md)
* [MariaDB MaxScale 2.4.3 Release Notes](Release-Notes/MaxScale-2.4.3-Release-Notes.md)
* [MariaDB MaxScale 2.4.2 Release Notes](Release-Notes/MaxScale-2.4.2-Release-Notes.md)
* [MariaDB MaxScale 2.4.1 Release Notes](Release-Notes/MaxScale-2.4.1-Release-Notes.md)
* [MariaDB MaxScale 2.4.0 Release Notes](Release-Notes/MaxScale-2.4.0-Release-Notes.md)

## MariaDB MaxScale 2.3

* Runtime Configuration of the Cache
* User Specified Syslog Facility and Level for Authentication Errors
* `config reload` removed from MaxAdmin (was deprecated in 2.2)
* MariaDBMonitor features added, modified and removed
* A Comment filter has been added.
* Services and filters can be created at runtime via the REST API
* Runtime router reconfiguration is now possible
* New Throttle filter that replaces and extends on the limit_queries functionality
* MaxCtrl
  * The `create monitor` command now accepts a list of key-value parameters
  * The new `drain server` drains the server of connections
  * A new interactive input mode was added
* Readwritesplit
  * Automatic transaction replay allows transactions to be migrated between servers
  * Master connections can now be re-opened
  * Writes with autocommit enabled can be automatically retried
  * Consistent reads on slaves via MASTER_GTID_WAIT
  * Transaction load balancing for normal transactions
  * Support for runtime router reconfiguration
  * A new load balancing method: ADAPTIVE_ROUTING
* Experimental resultset concatenation router, `cat`
* The schema router is now capable of table family sharding.
* The binlog router can now automatically switch to secondary masters
  when replicating from a Galera cluster in case the primary master
  goes down.
* MaxScale now has a systemd compatible watchdog.
* Server setting `authenticator_options` is no longer used and any value is
  ignored.

For more details, please refer to:

* [MariaDB MaxScale 2.3.20 Release Notes](Release-Notes/MaxScale-2.3.20-Release-Notes.md)
* [MariaDB MaxScale 2.3.19 Release Notes](Release-Notes/MaxScale-2.3.19-Release-Notes.md)
* [MariaDB MaxScale 2.3.18 Release Notes](Release-Notes/MaxScale-2.3.18-Release-Notes.md)
* [MariaDB MaxScale 2.3.17 Release Notes](Release-Notes/MaxScale-2.3.17-Release-Notes.md)
* [MariaDB MaxScale 2.3.16 Release Notes](Release-Notes/MaxScale-2.3.16-Release-Notes.md)
* [MariaDB MaxScale 2.3.15 Release Notes](Release-Notes/MaxScale-2.3.15-Release-Notes.md)
* [MariaDB MaxScale 2.3.14 Release Notes](Release-Notes/MaxScale-2.3.14-Release-Notes.md)
* [MariaDB MaxScale 2.3.13 Release Notes](Release-Notes/MaxScale-2.3.13-Release-Notes.md)
* [MariaDB MaxScale 2.3.12 Release Notes](Release-Notes/MaxScale-2.3.12-Release-Notes.md)
* [MariaDB MaxScale 2.3.11 Release Notes](Release-Notes/MaxScale-2.3.11-Release-Notes.md)
* [MariaDB MaxScale 2.3.10 Release Notes](Release-Notes/MaxScale-2.3.10-Release-Notes.md)
* [MariaDB MaxScale 2.3.9 Release Notes](Release-Notes/MaxScale-2.3.9-Release-Notes.md)
* [MariaDB MaxScale 2.3.8 Release Notes](Release-Notes/MaxScale-2.3.8-Release-Notes.md)
* [MariaDB MaxScale 2.3.7 Release Notes](Release-Notes/MaxScale-2.3.7-Release-Notes.md)
* [MariaDB MaxScale 2.3.6 Release Notes](Release-Notes/MaxScale-2.3.6-Release-Notes.md)
* [MariaDB MaxScale 2.3.5 Release Notes](Release-Notes/MaxScale-2.3.5-Release-Notes.md)
* [MariaDB MaxScale 2.3.4 Release Notes](Release-Notes/MaxScale-2.3.4-Release-Notes.md)
* [MariaDB MaxScale 2.3.3 Release Notes](Release-Notes/MaxScale-2.3.3-Release-Notes.md)
* [MariaDB MaxScale 2.3.2 Release Notes](Release-Notes/MaxScale-2.3.2-Release-Notes.md)
* [MariaDB MaxScale 2.3.1 Release Notes](Release-Notes/MaxScale-2.3.1-Release-Notes.md)
* [MariaDB MaxScale 2.3.0 Release Notes](Release-Notes/MaxScale-2.3.0-Release-Notes.md)

## MariaDB MaxScale 2.2

* Limited support from Pluggable Authentication Modules (PAM).
* Proxy protocol support for backend connections.
* REST-API for obtaining information about and for manipulating the
  resources of MaxScale.
* MaxCtrl, a new command line client for administering MaxScale
  implemented in terms of the REST-API.
* Firewall can now prevent the use of functions in conjunction with
  certain columns.
* Parser of MaxScale extended to support window functions and CTEs.
* Parser of MaxScale extended to support PL/SQL compatibility features
  of upcoming 10.3 release.
* Prepared statements are now parsed and the execution of read only
  ones will be routed to slaves.
* Server states are persisted, so in case of crash and restart MaxScale
  has the correct server state quicker.
* Monitor scripts are executed synchronously, so they can safely perform
  actions that change the server states.
* The Masking filter can now both obfuscate and partially mask columns.
* Binlog router supports MariaDB 10 GTID at both ends.
* KILL CONNECTION can now be used through MaxScale.
* Environment variables can now be used in the MaxScale configuration file.
* By default, MaxScale can no longer be run as root.
* The MySQL Monitor is now capable of performing failover and switchover of
  the master. There is also limited capability for rejoining nodes.

For more details, please refer to:

* [MariaDB MaxScale 2.2.21 Release Notes](Release-Notes/MaxScale-2.2.21-Release-Notes.md)
* [MariaDB MaxScale 2.2.20 Release Notes](Release-Notes/MaxScale-2.2.20-Release-Notes.md)
* [MariaDB MaxScale 2.2.19 Release Notes](Release-Notes/MaxScale-2.2.19-Release-Notes.md)
* [MariaDB MaxScale 2.2.18 Release Notes](Release-Notes/MaxScale-2.2.18-Release-Notes.md)
* [MariaDB MaxScale 2.2.17 Release Notes](Release-Notes/MaxScale-2.2.17-Release-Notes.md)
* [MariaDB MaxScale 2.2.16 Release Notes](Release-Notes/MaxScale-2.2.16-Release-Notes.md)
* [MariaDB MaxScale 2.2.15 Release Notes](Release-Notes/MaxScale-2.2.15-Release-Notes.md)
* [MariaDB MaxScale 2.2.14 Release Notes](Release-Notes/MaxScale-2.2.14-Release-Notes.md)
* [MariaDB MaxScale 2.2.13 Release Notes](Release-Notes/MaxScale-2.2.13-Release-Notes.md)
* [MariaDB MaxScale 2.2.12 Release Notes](Release-Notes/MaxScale-2.2.12-Release-Notes.md)
* [MariaDB MaxScale 2.2.11 Release Notes](Release-Notes/MaxScale-2.2.11-Release-Notes.md)
* [MariaDB MaxScale 2.2.10 Release Notes](Release-Notes/MaxScale-2.2.10-Release-Notes.md)
* [MariaDB MaxScale 2.2.9 Release Notes](Release-Notes/MaxScale-2.2.9-Release-Notes.md)
* [MariaDB MaxScale 2.2.8 Release Notes](Release-Notes/MaxScale-2.2.8-Release-Notes.md)
* [MariaDB MaxScale 2.2.7 Release Notes](Release-Notes/MaxScale-2.2.7-Release-Notes.md)
* [MariaDB MaxScale 2.2.6 Release Notes](Release-Notes/MaxScale-2.2.6-Release-Notes.md)
* [MariaDB MaxScale 2.2.5 Release Notes](Release-Notes/MaxScale-2.2.5-Release-Notes.md)
* [MariaDB MaxScale 2.2.4 Release Notes](Release-Notes/MaxScale-2.2.4-Release-Notes.md)
* [MariaDB MaxScale 2.2.3 Release Notes](Release-Notes/MaxScale-2.2.3-Release-Notes.md)
* [MariaDB MaxScale 2.2.2 Release Notes](Release-Notes/MaxScale-2.2.2-Release-Notes.md)
* [MariaDB MaxScale 2.2.1 Release Notes](Release-Notes/MaxScale-2.2.1-Release-Notes.md)
* [MariaDB MaxScale 2.2.0 Release Notes](Release-Notes/MaxScale-2.2.0-Release-Notes.md)

## MariaDB MaxScale 2.1

* MariaDB MaxScale is licensed under MariaDB BSL 1.1.
* Hierarchical configuration files are now supported.
* Logging is now performed in a way compatible with logrotate(8).
* Persistent connections are reset upon reuse.
* Galera monitor now consistently chooses the same node as master.
* Galera Monitor can set the preferred donor nodes list.
* The configuration can now be altered dynamically and the changes are persisted.
* There is now a monitor for Amazon Aurora clusters.
* MySQL Monitor now has a multi-master mode.
* MySQL Monitor now has a failover mode.
* Named Server Filter now supports wildcards for source option.
* Binlog Server can now be configured to encrypt binlog files.
* New filters, _cache_, _ccrfilter_, _insertstream_, _masking_, and _maxrows_ are introduced.
* GSSAPI based authentication can be used
* Prepared statements are now in the database firewall filtered exactly like non-prepared
  statements.
* The firewall filter can now filter based on function usage.
* MaxScale now supports IPv6

For more details, please refer to:

* [MariaDB MaxScale 2.1.17 Release Notes](Release-Notes/MaxScale-2.1.17-Release-Notes.md)
* [MariaDB MaxScale 2.1.16 Release Notes](Release-Notes/MaxScale-2.1.16-Release-Notes.md)
* [MariaDB MaxScale 2.1.15 Release Notes](Release-Notes/MaxScale-2.1.15-Release-Notes.md)
* [MariaDB MaxScale 2.1.14 Release Notes](Release-Notes/MaxScale-2.1.14-Release-Notes.md)
* [MariaDB MaxScale 2.1.13 Release Notes](Release-Notes/MaxScale-2.1.13-Release-Notes.md)
* [MariaDB MaxScale 2.1.12 Release Notes](Release-Notes/MaxScale-2.1.12-Release-Notes.md)
* [MariaDB MaxScale 2.1.11 Release Notes](Release-Notes/MaxScale-2.1.11-Release-Notes.md)
* [MariaDB MaxScale 2.1.10 Release Notes](Release-Notes/MaxScale-2.1.10-Release-Notes.md)
* [MariaDB MaxScale 2.1.9 Release Notes](Release-Notes/MaxScale-2.1.9-Release-Notes.md)
* [MariaDB MaxScale 2.1.8 Release Notes](Release-Notes/MaxScale-2.1.8-Release-Notes.md)
* [MariaDB MaxScale 2.1.7 Release Notes](Release-Notes/MaxScale-2.1.7-Release-Notes.md)
* [MariaDB MaxScale 2.1.6 Release Notes](Release-Notes/MaxScale-2.1.6-Release-Notes.md)
* [MariaDB MaxScale 2.1.5 Release Notes](Release-Notes/MaxScale-2.1.5-Release-Notes.md)
* [MariaDB MaxScale 2.1.4 Release Notes](Release-Notes/MaxScale-2.1.4-Release-Notes.md)
* [MariaDB MaxScale 2.1.3 Release Notes](Release-Notes/MaxScale-2.1.3-Release-Notes.md)
* [MariaDB MaxScale 2.1.2 Release Notes](Release-Notes/MaxScale-2.1.2-Release-Notes.md)
* [MariaDB MaxScale 2.1.1 Release Notes](Release-Notes/MaxScale-2.1.1-Release-Notes.md)
* [MariaDB MaxScale 2.1.0 Release Notes](Release-Notes/MaxScale-2.1.0-Release-Notes.md)

## MariaDB MaxScale 2.0
* MariaDB MaxScale is licensed under MariaDB BSL.
* SSL can be used in the communication between MariaDB MaxScale and the backend servers.
* The number of allowed connections can explicitly be throttled.
* MariaDB MaxScale can continue serving read request even if the master has gone down.
* The security of MaxAdmin has been improved; Unix domain sockets can be used in the
  communication with MariaDB MaxScale and the Linux identity can be used for authorization.
* MariaDB MaxScale can in real time make binlog events available as raw AVRO or
  as JSON objects (beta level functionality).

For more details, please refer to:

* [MariaDB MaxScale 2.0.6 Release Notes](Release-Notes/MaxScale-2.0.6-Release-Notes.md)
* [MariaDB MaxScale 2.0.5 Release Notes](Release-Notes/MaxScale-2.0.5-Release-Notes.md)
* [MariaDB MaxScale 2.0.4 Release Notes](Release-Notes/MaxScale-2.0.4-Release-Notes.md)
* [MariaDB MaxScale 2.0.3 Release Notes](Release-Notes/MaxScale-2.0.3-Release-Notes.md)
* [MariaDB MaxScale 2.0.2 Release Notes](Release-Notes/MaxScale-2.0.2-Release-Notes.md)
* [MariaDB MaxScale 2.0.1 Release Notes](Release-Notes/MaxScale-2.0.1-Release-Notes.md)
* [MariaDB MaxScale 2.0.0 Release Notes](Release-Notes/MaxScale-2.0.0-Release-Notes.md)

## MariaDB MaxScale 1.4

* Authentication now allows table level resolution of grants. MaxScale service
  users will now need SELECT privileges on `mysql.tables_priv` to be able to
  authenticate users at the database and table level.
* Firewall filter allows whitelisting.
* Client side SSL works.

For more details, please refer to

* [MariaDB MaxScale 1.4.3 Release Notes](Release-Notes/MaxScale-1.4.3-Release-Notes.md)
* [MariaDB MaxScale 1.4.2 Release Notes](Release-Notes/MaxScale-1.4.2-Release-Notes.md)
* [MariaDB MaxScale 1.4.1 Release Notes](Release-Notes/MaxScale-1.4.1-Release-Notes.md)
* [MariaDB MaxScale 1.4.0 Release Notes](Release-Notes/MaxScale-1.4.0-Release-Notes.md).

## MariaDB MaxScale 1.3
* Added support for persistent backend connections
* The *binlog server* is now an integral component of MariaDB MaxScale.
* The logging has been changed; instead of different log files there is one log file and different message priorities.

For more details, please refer to [MariaDB MaxScale 1.3 Release Notes](Release-Notes/MaxScale-1.3.0-Release-Notes.md)

## MariaDB MaxScale 1.2
* Logfiles have been renamed. The log names are now named error.log, messages.log, trace.log and debug.log.

## MariaDB MaxScale 1.1.1

* Schemarouter now also allows for an upper limit to session commands.
* Schemarouter correctly handles SHOW DATABASES responses that span multiple buffers.
* Readwritesplit and Schemarouter now allow disabling of the session command history.

## MariaDB MaxScale 1.1

**NOTE:** MariaDB MaxScale default installation directory has changed to `/usr/local/mariadb-maxscale` and the default password for MaxAdmin is now ´mariadb´.

* New modules added
      * Binlog router
      * Firewall filter
      * Multi-Master monitor
      * RabbitMQ logging filter
      * Schema Sharding router
* Added option to use high precision timestamps in logging.
* Readwritesplit router now returns the master server's response.
* New readwritesplit router option added. It is now possible to control the amount of memory readwritesplit sessions will consume by limiting the amount of session modifying statements they can execute.
* Minimum required CMake version is now 2.8.12 for package building.
* Session idle timeout added for services. More details can be found in the configuration guide.
* Monitor API is updated to 2.0.0. Monitors with earlier versions of the API no longer work with this version of MariaDB MaxScale.
* MariaDB MaxScale now requires libcurl and libcurl development headers.
* Nagios plugins added.
* Notification service added.
* Readconnrouter has a new "running" router_option. This allows it to use any running server as a valid backend server.
* Database names can be stripped of escape characters with the `strip_db_esc` service parameter.
