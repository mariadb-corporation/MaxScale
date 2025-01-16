# Configuration Settings
[TOC]
## General
### [MaxScale](../Getting-Started/Configuration-Guide.md)
#### Global Settings
##### [admin_audit](../Getting-Started/Configuration-Guide.md#admin_audit)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [admin_audit_exclude_methods](../Getting-Started/Configuration-Guide.md#admin_audit_exclude_methods)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `GET`, `PUT`, `POST`, `PATCH`, `DELETE`, `HEAD`, `OPTIONS`, `CONNECT`, `TRACE`
- **Default**: No exclusions

##### [admin_audit_file](../Getting-Started/Configuration-Guide.md#admin_audit_file)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `/var/log/maxscale/admin_audit.csv`

##### [admin_auth](../Getting-Started/Configuration-Guide.md#admin_auth)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `true`

##### [admin_enabled](../Getting-Started/Configuration-Guide.md#admin_enabled)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `true`

##### [admin_gui](../Getting-Started/Configuration-Guide.md#admin_gui)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `true`

##### [admin_host](../Getting-Started/Configuration-Guide.md#admin_host)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `"127.0.0.1"`

##### [admin_jwt_algorithm](../Getting-Started/Configuration-Guide.md#admin_jwt_algorithm)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `auto`, `HS256`, `HS384`, `HS512`, `RS256`, `RS384`, `RS512`, `PS256`, `PS384`, `PS512`, `ES256`, `ES384`, `ES512`, `ED25519`, `ED448`
- **Default**: `auto`

##### [admin_jwt_issuer](../Getting-Started/Configuration-Guide.md#admin_jwt_issuer)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `maxscale`

##### [admin_jwt_key](../Getting-Started/Configuration-Guide.md#admin_jwt_key)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [admin_jwt_max_age](../Getting-Started/Configuration-Guide.md#admin_jwt_max_age)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `24h`

##### [admin_log_auth_failures](../Getting-Started/Configuration-Guide.md#admin_log_auth_failures)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [admin_oidc_url](../Getting-Started/Configuration-Guide.md#admin_oidc_url)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [admin_pam_readonly_service](../Getting-Started/Configuration-Guide.md#admin_pam_readonly_service)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [admin_pam_readwrite_service](../Getting-Started/Configuration-Guide.md#admin_pam_readwrite_service)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [admin_port](../Getting-Started/Configuration-Guide.md#admin_port)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `8989`

##### [admin_readwrite_hosts](../Getting-Started/Configuration-Guide.md#admin_readwrite_hosts)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `%`

##### [admin_secure_gui](../Getting-Started/Configuration-Guide.md#admin_secure_gui)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `true`

##### [admin_ssl_ca](../Getting-Started/Configuration-Guide.md#admin_ssl_ca)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [admin_ssl_cert](../Getting-Started/Configuration-Guide.md#admin_ssl_cert)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [admin_ssl_cipher](../Getting-Started/Configuration-Guide.md#admin_ssl_cipher)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No

##### [admin_ssl_key](../Getting-Started/Configuration-Guide.md#admin_ssl_key)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [admin_ssl_version](../Getting-Started/Configuration-Guide.md#admin_ssl_version)
- **Type**: [enum_mask](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `MAX`, `TLSv1.0`, `TLSv1.1`, `TLSv1.2`, `TLSv1.3`, `TLSv10`, `TLSv11`, `TLSv12`, `TLSv13`
- **Default**: `MAX`

##### [admin_verify_url](../Getting-Started/Configuration-Guide.md#admin_verify_url)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [auth_connect_timeout](../Getting-Started/Configuration-Guide.md#auth_connect_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `10s`

##### [auto_tune](../Getting-Started/Configuration-Guide.md#auto_tune)
- **Type**: string list
- **Values**: `all` or list of auto tunable parameters, separated by `,`
- **Default**: No
- **Mandatory**: No
- **Dynamic**: No

##### [cachedir](../Getting-Started/Configuration-Guide.md#cachedir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `/var/cache/maxscale`

##### [config_sync_cluster](../Getting-Started/Configuration-Guide.md#config_sync_cluster)
- **Type**: monitor
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [config_sync_db](../Getting-Started/Configuration-Guide.md#config_sync_db)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `mysql`

##### [config_sync_interval](../Getting-Started/Configuration-Guide.md#config_sync_interval)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `5s`

##### [config_sync_password](../Getting-Started/Configuration-Guide.md#config_sync_password)
- **Type**: password
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [config_sync_timeout](../Getting-Started/Configuration-Guide.md#config_sync_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `10s`

##### [config_sync_user](../Getting-Started/Configuration-Guide.md#config_sync_user)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [connector_plugindir](../Getting-Started/Configuration-Guide.md#connector_plugindir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: OS Dependent

##### [core_file](../Getting-Started/Configuration-Guide.md#core_file)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Default**: true
- **Dynamic**: No

##### [datadir](../Getting-Started/Configuration-Guide.md#datadir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `/var/lib/maxscale`

##### [debug](../Getting-Started/Configuration-Guide.md#debug)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [dump_last_statements](../Getting-Started/Configuration-Guide.md#dump_last_statements)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `on_close`, `on_error`, `never`
- **Default**: `never`

##### [execdir](../Getting-Started/Configuration-Guide.md#execdir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `/usr/bin`

##### [host_cache_size](../Getting-Started/Configuration-Guide.md#host_cache_size)
- **Type**: integer
- **Default**: 128
- **Dynamic**: Yes

##### [key_manager](../Getting-Started/Configuration-Guide.md#key_manager)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Dynamic**: Yes
- **Values**: `none`, `file`, `kmip`, `vault`
- **Default**: `none`

##### [language](../Getting-Started/Configuration-Guide.md#language)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `/var/lib/maxscale/`

##### [libdir](../Getting-Started/Configuration-Guide.md#libdir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: OS Dependent

##### [load_persisted_configs](../Getting-Started/Configuration-Guide.md#load_persisted_configs)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `true`

##### [local_address](../Getting-Started/Configuration-Guide.md#local_address)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [log_augmentation](../Getting-Started/Configuration-Guide.md#log_augmentation)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0`

##### [log_debug](../Getting-Started/Configuration-Guide.md#log_debug)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [log_info](../Getting-Started/Configuration-Guide.md#log_info)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [log_notice](../Getting-Started/Configuration-Guide.md#log_notice)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [log_throttling](../Getting-Started/Configuration-Guide.md#log_throttling)
- **Type**: number, [duration](../Getting-Started/Configuration-Guide.md#durations), [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `10, 1000ms, 10000ms`

##### [log_warn_super_user](../Getting-Started/Configuration-Guide.md#log_warn_super_user)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `false`

##### [log_warning](../Getting-Started/Configuration-Guide.md#log_warning)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [logdir](../Getting-Started/Configuration-Guide.md#logdir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `/var/log/maxscale`

##### [max_auth_errors_until_block](../Getting-Started/Configuration-Guide.md#max_auth_errors_until_block)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `10`

##### [maxlog](../Getting-Started/Configuration-Guide.md#maxlog)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [module_configdir](../Getting-Started/Configuration-Guide.md#module_configdir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `/etc/maxscale.modules.d/`

##### [ms_timestamp](../Getting-Started/Configuration-Guide.md#ms_timestamp)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [passive](../Getting-Started/Configuration-Guide.md#passive)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [persist_runtime_changes](../Getting-Started/Configuration-Guide.md#persist_runtime_changes)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Default**: true
- **Dynamic**: No

##### [persistdir](../Getting-Started/Configuration-Guide.md#persistdir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `/var/lib/maxscale/maxscale.cnf.d/`

##### [piddir](../Getting-Started/Configuration-Guide.md#piddir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `/run/maxscale`

##### [query_classifier_cache_size](../Getting-Started/Configuration-Guide.md#query_classifier_cache_size)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: System Dependent

##### [query_retries](../Getting-Started/Configuration-Guide.md#query_retries)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `1`

##### [query_retry_timeout](../Getting-Started/Configuration-Guide.md#query_retry_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `10s`

##### [rebalance_period](../Getting-Started/Configuration-Guide.md#rebalance_period)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0s`

##### [rebalance_threshold](../Getting-Started/Configuration-Guide.md#rebalance_threshold)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `20`

##### [rebalance_window](../Getting-Started/Configuration-Guide.md#rebalance_window)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `10`

##### [retain_last_statements](../Getting-Started/Configuration-Guide.md#retain_last_statements)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0`

##### [secretsdir](../Getting-Started/Configuration-Guide.md#secretsdir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [session_trace](../Getting-Started/Configuration-Guide.md#session_trace)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0`

##### [session_trace_match](../Getting-Started/Configuration-Guide.md#session_trace_match)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [sharedir](../Getting-Started/Configuration-Guide.md#sharedir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `/usr/share/maxscale`

##### [skip_name_resolve](../Getting-Started/Configuration-Guide.md#skip_name_resolve)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [sql_mode](../Getting-Started/Configuration-Guide.md#sql_mode)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `default`, `oracle`
- **Default**: `default`

##### [substitute_variables](../Getting-Started/Configuration-Guide.md#substitute_variables)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `false`

##### [syslog](../Getting-Started/Configuration-Guide.md#syslog)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [threads](../Getting-Started/Configuration-Guide.md#threads)
- **Type**: number or `auto`
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `auto`

##### [threads_max](../Getting-Started/Configuration-Guide.md#threads_max)
- **Type**: positive integer
- **Default**: 256
- **Dynamic**: No

##### [trace_file_dir](../Getting-Started/Configuration-Guide.md#trace_file_dir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No

##### [trace_file_size](../Getting-Started/Configuration-Guide.md#trace_file_size)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: Yes

##### [users_refresh_interval](../Getting-Started/Configuration-Guide.md#users_refresh_interval)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0s`

##### [users_refresh_time](../Getting-Started/Configuration-Guide.md#users_refresh_time)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `30s`

##### [writeq_high_water](../Getting-Started/Configuration-Guide.md#writeq_high_water)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#size)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `65536`

##### [writeq_low_water](../Getting-Started/Configuration-Guide.md#writeq_low_water)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#size)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `1024`


#### Listener
##### [address](../Getting-Started/Configuration-Guide.md#address)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `"::"`

##### [authenticator](../Getting-Started/Configuration-Guide.md#authenticator)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [authenticator_options](../Getting-Started/Configuration-Guide.md#authenticator_options)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [connection_init_sql_file](../Getting-Started/Configuration-Guide.md#connection_init_sql_file)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [connection_metadata](../Getting-Started/Configuration-Guide.md#connection_metadata)
- **Type**: stringlist
- **Default**: `character_set_client=auto,character_set_connection=auto,character_set_results=auto,max_allowed_packet=auto,system_time_zone=auto,time_zone=auto,tx_isolation=auto,maxscale=auto`
- **Dynamic**: Yes
- **Mandatory**: No

##### [port](../Getting-Started/Configuration-Guide.md#port)
- **Type**: number
- **Mandatory**: Yes, if `socket` is not provided.
- **Dynamic**: No
- **Default**: `0`

##### [protocol](../Getting-Started/Configuration-Guide.md#protocol)
- **Type**: protocol
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `mariadb`

##### [service](../Getting-Started/Configuration-Guide.md#service)
- **Type**: service
- **Mandatory**: Yes
- **Dynamic**: No

##### [socket](../Getting-Started/Configuration-Guide.md#socket)
- **Type**: string
- **Mandatory**: Yes, if `port` is not provided.
- **Dynamic**: No
- **Default**: `""`

##### [sql_mode](../Getting-Started/Configuration-Guide.md#sql_mode)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumeration)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `default`, `oracle`
- **Default**: `default`

##### [user_mapping_file](../Getting-Started/Configuration-Guide.md#user_mapping_file)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`


#### Server
##### [address](../Getting-Started/Configuration-Guide.md#address)
- **Type**: string
- **Mandatory**: Yes, if `socket` is not provided.
- **Dynamic**: Yes
- **Default**: `""`

##### [disk_space_threshold](../Getting-Started/Configuration-Guide.md#disk_space_threshold)
- **Type**: Custom
- **Mandatory**: No
- **Dynamic**: No
- **Default**: None

##### [extra_port](../Getting-Started/Configuration-Guide.md#extra_port)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0`

##### [max_routing_connections](../Getting-Started/Configuration-Guide.md#max_routing_connections)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0`

##### [monitorpw](../Getting-Started/Configuration-Guide.md#monitorpw)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [monitoruser](../Getting-Started/Configuration-Guide.md#monitoruser)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [persistmaxtime](../Getting-Started/Configuration-Guide.md#persistmaxtime)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0s`

##### [persistpoolmax](../Getting-Started/Configuration-Guide.md#persistpoolmax)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0`

##### [port](../Getting-Started/Configuration-Guide.md#port)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `3306`

##### [priority](../Getting-Started/Configuration-Guide.md#priority)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 0

##### [private_address](../Getting-Started/Configuration-Guide.md#private_address)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [proxy_protocol](../Getting-Started/Configuration-Guide.md#proxy_protocol)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [rank](../Getting-Started/Configuration-Guide.md#rank)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `primary`, `secondary`
- **Default**: `primary`

##### [replication_custom_options](../Getting-Started/Configuration-Guide.md#replication_custom_options)
- **Type**: string
- **Default**: None
- **Dynamic**: Yes

##### [socket](../Getting-Started/Configuration-Guide.md#socket)
- **Type**: string
- **Mandatory**: Yes, if `address` is not provided.
- **Dynamic**: Yes
- **Default**: `""`


#### Service
##### [auth_all_servers](../Getting-Started/Configuration-Guide.md#auth_all_servers)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [cluster](../Getting-Started/Configuration-Guide.md#cluster)
- **Type**: monitor
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [connection_keepalive](../Getting-Started/Configuration-Guide.md#connection_keepalive)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `300s`
- **Auto tune**: [Yes](../Getting-Started/Configuration-Guide.md#auto_tune)

##### [disable_sescmd_history](../Getting-Started/Configuration-Guide.md#disable_sescmd_history)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [enable_root_user](../Getting-Started/Configuration-Guide.md#enable_root_user)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [filters](../Getting-Started/Configuration-Guide.md#filters)
- **Type**: filter list
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [force_connection_keepalive](../Getting-Started/Configuration-Guide.md#force_connection_keepalive)
- **Type**: boolean
- **Mandatory** No
- **Dynamic**: Yes
- **Default**: `false`

##### [idle_session_pool_time](../Getting-Started/Configuration-Guide.md#idle_session_pool_time)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `-1s`

##### [log_auth_warnings](../Getting-Started/Configuration-Guide.md#log_auth_warnings)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [log_debug](../Getting-Started/Configuration-Guide.md#log_debug)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [log_info](../Getting-Started/Configuration-Guide.md#log_info)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [log_notice](../Getting-Started/Configuration-Guide.md#log_notice)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [log_warning](../Getting-Started/Configuration-Guide.md#log_warning)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [max_connections](../Getting-Started/Configuration-Guide.md#max_connections)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0`

##### [max_sescmd_history](../Getting-Started/Configuration-Guide.md#max_sescmd_history)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `50`

##### [multiplex_timeout](../Getting-Started/Configuration-Guide.md#multiplex_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `60s`

##### [net_write_timeout](../Getting-Started/Configuration-Guide.md#net_write_timeout)
- **Type**: [durations](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory** No
- **Dynamic**: Yes
- **Default**: `0s`

##### [password](../Getting-Started/Configuration-Guide.md#password)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [prune_sescmd_history](../Getting-Started/Configuration-Guide.md#prune_sescmd_history)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [retain_last_statements](../Getting-Started/Configuration-Guide.md#retain_last_statements)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `-1`

##### [router](../Getting-Started/Configuration-Guide.md#router)
- **Type**: router
- **Mandatory**: Yes
- **Dynamic**: No

##### [servers](../Getting-Started/Configuration-Guide.md#servers)
- **Type**: server list
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [session_track_trx_state](../Getting-Started/Configuration-Guide.md#session_track_trx_state)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [strip_db_esc](../Getting-Started/Configuration-Guide.md#strip_db_esc)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [targets](../Getting-Started/Configuration-Guide.md#targets)
- **Type**: target list
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [user](../Getting-Started/Configuration-Guide.md#user)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [user_accounts_file](../Getting-Started/Configuration-Guide.md#user_accounts_file)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [user_accounts_file_usage](../Getting-Started/Configuration-Guide.md#user_accounts_file_usage)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `add_when_load_ok`, `file_only_always`
- **Default**: `add_when_load_ok`

##### [version_string](../Getting-Started/Configuration-Guide.md#version_string)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: None

##### [wait_timeout](../Getting-Started/Configuration-Guide.md#wait_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0s`
- **Auto tune**: [Yes](../Getting-Started/Configuration-Guide.md#auto_tune)


#### Settings for File-based Key Manager
##### [file.keyfile](../Getting-Started/Configuration-Guide.md#file.keyfile)
- **Type**: path
- **Mandatory**: Yes
- **Dynamic**: Yes


#### Settings for HashiCorp Vault Key Manager
##### [vault.ca](../Getting-Started/Configuration-Guide.md#vault.ca)
- **Type**: path
- **Default**: `""`
- **Dynamic**: Yes

##### [vault.host](../Getting-Started/Configuration-Guide.md#vault.host)
- **Type**: string
- **Default**: `localhost`
- **Dynamic**: Yes

##### [vault.mount](../Getting-Started/Configuration-Guide.md#vault.mount)
- **Type**: string
- **Default**: `secret`
- **Dynamic**: Yes

##### [vault.port](../Getting-Started/Configuration-Guide.md#vault.port)
- **Type**: integer
- **Default**: `8200`
- **Dynamic**: Yes

##### [vault.timeout](../Getting-Started/Configuration-Guide.md#vault.timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Default**: 30s
- **Dynamic**: Yes

##### [vault.tls](../Getting-Started/Configuration-Guide.md#vault.tls)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Default**: true
- **Dynamic**: Yes

##### [vault.token](../Getting-Started/Configuration-Guide.md#vault.token)
- **Type**: password
- **Mandatory**: Yes
- **Dynamic**: Yes


#### Settings for KMIP Key Manager
##### [kmip.ca](../Getting-Started/Configuration-Guide.md#kmip.ca)
- **Type**: path
- **Default**: `""`
- **Dynamic**: Yes

##### [kmip.cert](../Getting-Started/Configuration-Guide.md#kmip.cert)
- **Type**: path
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [kmip.host](../Getting-Started/Configuration-Guide.md#kmip.host)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [kmip.key](../Getting-Started/Configuration-Guide.md#kmip.key)
- **Type**: path
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [kmip.port](../Getting-Started/Configuration-Guide.md#kmip.port)
- **Type**: integer
- **Mandatory**: Yes
- **Dynamic**: Yes


#### Settings for TLS/SSL Encryption
##### [ssl](../Getting-Started/Configuration-Guide.md#ssl)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [ssl_ca](../Getting-Started/Configuration-Guide.md#ssl_ca)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [ssl_cert](../Getting-Started/Configuration-Guide.md#ssl_cert)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [ssl_cert_verify_depth](../Getting-Started/Configuration-Guide.md#ssl_cert_verify_depth)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `9`

##### [ssl_cipher](../Getting-Started/Configuration-Guide.md#ssl_cipher)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [ssl_crl](../Getting-Started/Configuration-Guide.md#ssl_crl)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [ssl_key](../Getting-Started/Configuration-Guide.md#ssl_key)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [ssl_verify_peer_certificate](../Getting-Started/Configuration-Guide.md#ssl_verify_peer_certificate)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [ssl_verify_peer_host](../Getting-Started/Configuration-Guide.md#ssl_verify_peer_host)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory** No
- **Dynamic**: Yes
- **Default**: `false`

##### [ssl_version](../Getting-Started/Configuration-Guide.md#ssl_version)
- **Type**: [enum_mask](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `MAX`, `TLSv1.0`, `TLSv1.1`, `TLSv1.2`, `TLSv1.3`, `TLSv10`, `TLSv11`, `TLSv12`, `TLSv13`
- **Default**: `MAX`



## Authenticators
### [Authentication-Modules](../Authenticators/Authentication-Modules.md)
#### Settings
##### [lower_case_table_names](../Authenticators/Authentication-Modules.md#lower_case_table_names)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0`

##### [match_host](../Authenticators/Authentication-Modules.md#match_host)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `true`

##### [skip_authentication](../Authenticators/Authentication-Modules.md#skip_authentication)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `false`



### [GSSAPI-Authenticator](../Authenticators/GSSAPI-Authenticator.md)
#### Settings
##### [gssapi_keytab_path](../Authenticators/GSSAPI-Authenticator.md#gssapi_keytab_path)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: Kerberos Default

##### [principal_name](../Authenticators/GSSAPI-Authenticator.md#principal_name)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `mariadb/localhost.localdomain`



### [MySQL-Authenticator](../Authenticators/MySQL-Authenticator.md)
#### Settings
##### [log_password_mismatch](../Authenticators/MySQL-Authenticator.md#log_password_mismatch)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `false`



### [PAM-Authenticator](../Authenticators/PAM-Authenticator.md)
#### Settings
##### [pam_backend_mapping](../Authenticators/PAM-Authenticator.md#pam_backend_mapping)
- **Type**: [enumeration](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `none`, `mariadb`
- **Default**: `none`

##### [pam_mapped_pw_file](../Authenticators/PAM-Authenticator.md#pam_mapped_pw_file)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: None

##### [pam_mode](../Authenticators/PAM-Authenticator.md#pam_mode)
- **Type**: [enumeration](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `password`, `password_2FA`, `suid`
- **Default**: `password`

##### [pam_use_cleartext_plugin](../Authenticators/PAM-Authenticator.md#pam_use_cleartext_plugin)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `false`




## Filters
### [BinlogFilter](../Filters/BinlogFilter.md)
#### Settings
##### [exclude](../Filters/BinlogFilter.md#exclude)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [match](../Filters/BinlogFilter.md#match)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [rewrite_dest](../Filters/BinlogFilter.md#rewrite_dest)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [rewrite_src](../Filters/BinlogFilter.md#rewrite_src)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None



### [CCRFilter](../Filters/CCRFilter.md)
#### Settings
##### [count](../Filters/CCRFilter.md#count)
- **Type**: count
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0`

##### [global](../Filters/CCRFilter.md#global)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [ignore](../Filters/CCRFilter.md#ignore)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [match](../Filters/CCRFilter.md#match)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [options](../Filters/CCRFilter.md#options)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `ignorecase`, `case`, `extended`
- **Default**: `ignorecase`

##### [time](../Filters/CCRFilter.md#time)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `60s`



### [Cache](../Filters/Cache.md)
#### Settings
##### [cache_in_transactions](../Filters/Cache.md#cache_in_transactions)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `never`, `read_only_transactions`, `all_transactions`
- **Default**: `all_transactions`

##### [cached_data](../Filters/Cache.md#cached_data)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `shared`, `thread_specific`
- **Default**: `thread_specific`

##### [clear_cache_on_parse_errors](../Filters/Cache.md#clear_cache_on_parse_errors)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `true`

##### [debug](../Filters/Cache.md#debug)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0`

##### [enabled](../Filters/Cache.md#enabled)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `true`

##### [hard_ttl](../Filters/Cache.md#hard_ttl)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0s` (no limit)

##### [invalidate](../Filters/Cache.md#invalidate)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `never`, `current`
- **Default**: `never`

##### [max_count](../Filters/Cache.md#max_count)
- **Type**: count
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0` (no limit)

##### [max_resultset_rows](../Filters/Cache.md#max_resultset_rows)
- **Type**: count
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0` (no limit)

##### [max_resultset_size](../Filters/Cache.md#max_resultset_size)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0` (no limit)

##### [max_size](../Filters/Cache.md#max_size)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0` (no limit)

##### [rules](../Filters/Cache.md#rules)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""` (no rules)

##### [selects](../Filters/Cache.md#selects)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `assume_cacheable`, `verify_cacheable`
- **Default**: `assume_cacheable`

##### [soft_ttl](../Filters/Cache.md#soft_ttl)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0s` (no limit)

##### [storage](../Filters/Cache.md#storage)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `storage_inmemory`

##### [storage_options](../Filters/Cache.md#storage_options)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**:

##### [timeout](../Filters/Cache.md#timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `5s`

##### [users](../Filters/Cache.md#users)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `mixed`, `isolated`
- **Default**: `mixed`


#### `storage_memcached`
##### [max_value_size](../Filters/Cache.md#max_value_size)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: 1Mi

##### [server](../Filters/Cache.md#server)
- **Type**: The Memcached server address specified as `host[:port]`
- **Mandatory**: Yes
- **Dynamic**: No


#### `storage_redis`
##### [password](../Filters/Cache.md#password)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [server](../Filters/Cache.md#server)
- **Type**: The Redis server address specified as `host[:port]`
- **Mandatory**: Yes
- **Dynamic**: No

##### [ssl](../Filters/Cache.md#ssl)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `false`

##### [ssl_ca](../Filters/Cache.md#ssl_ca)
- **Type**: Path to existing readable file.
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [ssl_cert](../Filters/Cache.md#ssl_cert)
- **Type**: Path to existing readable file.
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [ssl_key](../Filters/Cache.md#ssl_key)
- **Type**: Path to existing readable file.
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [username](../Filters/Cache.md#username)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`



### [Comment](../Filters/Comment.md)
#### Settings
##### [inject](../Filters/Comment.md#inject)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes



### [LDIFilter](../Filters/LDIFilter.md)
#### Settings
##### [host](../Filters/LDIFilter.md#host)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `s3.amazonaws.com`

##### [key](../Filters/LDIFilter.md#key)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes

##### [no_verify](../Filters/LDIFilter.md#no_verify)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [port](../Filters/LDIFilter.md#port)
- **Type**: integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 0

##### [protocol_version](../Filters/LDIFilter.md#protocol_version)
- **Type**: integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 0
- **Values**: 0, 1, 2

##### [region](../Filters/LDIFilter.md#region)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `us-east-1`

##### [secret](../Filters/LDIFilter.md#secret)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes

##### [use_http](../Filters/LDIFilter.md#use_http)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false



### [Masking](../Filters/Masking.md)
#### Settings
##### [check_subqueries](../Filters/Masking.md#check_subqueries)
- **Type**: [bool](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [check_unions](../Filters/Masking.md#check_unions)
- **Type**: [bool](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [check_user_variables](../Filters/Masking.md#check_user_variables)
- **Type**: [bool](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [large_payload](../Filters/Masking.md#large_payload)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `ignore`, `abort`
- **Default**: `abort`

##### [prevent_function_usage](../Filters/Masking.md#prevent_function_usage)
- **Type**: [bool](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [require_fully_parsed](../Filters/Masking.md#require_fully_parsed)
- **Type**: [bool](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [rules](../Filters/Masking.md#rules)
- **Type**: path
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [treat_string_arg_as_field](../Filters/Masking.md#treat_string_arg_as_field)
- **Type**: [bool](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [warn_type_mismatch](../Filters/Masking.md#warn_type_mismatch)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `never`, `always`
- **Default**: `never`



### [Maxrows](../Filters/Maxrows.md)
#### Settings
##### [debug](../Filters/Maxrows.md#debug)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0`

##### [max_resultset_return](../Filters/Maxrows.md#max_resultset_return)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `empty`, `error`, `ok`
- **Default**: `empty`

##### [max_resultset_rows](../Filters/Maxrows.md#max_resultset_rows)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: (no limit)

##### [max_resultset_size](../Filters/Maxrows.md#max_resultset_size)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `64Ki`



### [Named-Server-Filter](../Filters/Named-Server-Filter.md)
#### Settings
##### [matchXY](../Filters/Named-Server-Filter.md#matchXY)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [options](../Filters/Named-Server-Filter.md#options)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `ignorecase`, `case`, `extended`
- **Default**: `ignorecase`

##### [source](../Filters/Named-Server-Filter.md#source)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [targetXY](../Filters/Named-Server-Filter.md#targetXY)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [user](../Filters/Named-Server-Filter.md#user)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None



### [Query-Log-All-Filter](../Filters/Query-Log-All-Filter.md)
#### Settings
##### [append](../Filters/Query-Log-All-Filter.md#append)
- **Type**: [bool](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [duration_unit](../Filters/Query-Log-All-Filter.md#duration_unit)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `milliseconds`

##### [exclude](../Filters/Query-Log-All-Filter.md#exclude)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [filebase](../Filters/Query-Log-All-Filter.md#filebase)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: No

##### [flush](../Filters/Query-Log-All-Filter.md#flush)
- **Type**: [bool](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [log_data](../Filters/Query-Log-All-Filter.md#log_data)
- **Type**: [enum_mask](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `service`, `session`, `date`, `user`, `reply_time`, `total_reply_time`, `query`, `default_db`, `num_rows`, `reply_size`, `transaction`, `transaction_time`, `num_warnings`, `error_msg`
- **Default**: `date, user, query`

##### [log_type](../Filters/Query-Log-All-Filter.md#log_type)
- **Type**: [enum_mask](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `session`, `unified`, `stdout`
- **Default**: `session`

##### [match](../Filters/Query-Log-All-Filter.md#match)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [newline_replacement](../Filters/Query-Log-All-Filter.md#newline_replacement)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `" "`

##### [options](../Filters/Query-Log-All-Filter.md#options)
- **Type**: [enum_mask](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `case`, `ignorecase`, `extended`
- **Default**: `case`

##### [separator](../Filters/Query-Log-All-Filter.md#separator)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `","`

##### [source](../Filters/Query-Log-All-Filter.md#source)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [source_exclude](../Filters/Query-Log-All-Filter.md#source_exclude)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes

##### [source_match](../Filters/Query-Log-All-Filter.md#source_match)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes

##### [use_canonical_form](../Filters/Query-Log-All-Filter.md#use_canonical_form)
- **Type**: [bool](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [user](../Filters/Query-Log-All-Filter.md#user)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [user_exclude](../Filters/Query-Log-All-Filter.md#user_exclude)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes

##### [user_match](../Filters/Query-Log-All-Filter.md#user_match)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes



### [Regex-Filter](../Filters/Regex-Filter.md)
#### Settings
##### [log_file](../Filters/Regex-Filter.md#log_file)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [log_trace](../Filters/Regex-Filter.md#log_trace)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [match](../Filters/Regex-Filter.md#match)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [options](../Filters/Regex-Filter.md#options)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `ignorecase`, `case`, `extended`
- **Default**: `ignorecase`

##### [replace](../Filters/Regex-Filter.md#replace)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [source](../Filters/Regex-Filter.md#source)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [user](../Filters/Regex-Filter.md#user)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None



### [RewriteFilter](../Filters/RewriteFilter.md)
#### Settings
##### [case_sensitive](../Filters/RewriteFilter.md#case_sensitive)
- **Type**: boolean
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true

##### [log_replacement](../Filters/RewriteFilter.md#log_replacement)
- **Type**: boolean
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [regex_grammar](../Filters/RewriteFilter.md#regex_grammar)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: Native
- **Values**: `Native`, `ECMAScript`, `Posix`, `EPosix`, `Awk`, `Grep`, `EGrep`

##### [template_file](../Filters/RewriteFilter.md#template_file)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes
- **Default**: No default value


#### Settings per template in the template file
##### [case_sensitive](../Filters/RewriteFilter.md#case_sensitive)
- **Type**: boolean
- **Default**: From maxscale.cnf

##### [continue_if_matched](../Filters/RewriteFilter.md#continue_if_matched)
- **Type**: boolean
- **Default**: false

##### [ignore_whitespace](../Filters/RewriteFilter.md#ignore_whitespace)
- **Type**: boolean
- **Default**: true

##### [regex_grammar](../Filters/RewriteFilter.md#regex_grammar)
- **Type**: string
- **Values**: `Native`, `ECMAScript`, `Posix`, `EPosix`, `Awk`, `Grep`, `EGrep`
- **Default**: From maxscale.cnf

##### [what_if](../Filters/RewriteFilter.md#what_if)
- **Type**: boolean
- **Default**: false



### [Tee-Filter](../Filters/Tee-Filter.md)
#### Settings
##### [exclude](../Filters/Tee-Filter.md#exclude)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [match](../Filters/Tee-Filter.md#match)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [options](../Filters/Tee-Filter.md#options)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `ignorecase`, `case`, `extended`
- **Default**: `ignorecase`

##### [service](../Filters/Tee-Filter.md#service)
- **Type**: service
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: none

##### [source](../Filters/Tee-Filter.md#source)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [sync](../Filters/Tee-Filter.md#sync)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [target](../Filters/Tee-Filter.md#target)
- **Type**: target
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: none

##### [user](../Filters/Tee-Filter.md#user)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None



### [Throttle](../Filters/Throttle.md)
#### Settings
##### [continuous_duration](../Filters/Throttle.md#continuous_duration)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 2s

##### [max_qps](../Filters/Throttle.md#max_qps)
- **Type**: number
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [sampling_duration](../Filters/Throttle.md#sampling_duration)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 250ms

##### [throttling_duration](../Filters/Throttle.md#throttling_duration)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: Yes
- **Dynamic**: Yes



### [Top-N-Filter](../Filters/Top-N-Filter.md)
#### Settings
##### [count](../Filters/Top-N-Filter.md#count)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `10`

##### [exclude](../Filters/Top-N-Filter.md#exclude)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [filebase](../Filters/Top-N-Filter.md#filebase)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [match](../Filters/Top-N-Filter.md#match)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [options](../Filters/Top-N-Filter.md#options)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `ignorecase`, `case`, `extended`
- **Default**: `case`

##### [source](../Filters/Top-N-Filter.md#source)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [user](../Filters/Top-N-Filter.md#user)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None



### [Wcar](../Filters/Wcar.md)
#### Settings
##### [capture_dir](../Filters/Wcar.md#capture_dir)
- **Type**: path
- **Default**: /var/lib/maxscale/wcar/
- **Mandatory**: No
- **Dynamic**: No

##### [capture_duration](../Filters/Wcar.md#capture_duration)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Default**: 0s
- **Mandatory**: No
- **Dynamic**: No

##### [capture_size](../Filters/Wcar.md#capture_size)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Default**: 0
- **Mandatory**: No
- **Dynamic**: No

##### [start_capture](../Filters/Wcar.md#start_capture)
- **Type**: boolean
- **Default**: false
- **Mandatory**: No
- **Dynamic**: No




## Monitors
### [Galera-Monitor](../Monitors/Galera-Monitor.md)
#### Settings
##### [available_when_donor](../Monitors/Galera-Monitor.md#available_when_donor)
- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

##### [disable_master_failback](../Monitors/Galera-Monitor.md#disable_master_failback)
- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

##### [disable_master_role_setting](../Monitors/Galera-Monitor.md#disable_master_role_setting)
- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

##### [root_node_as_master](../Monitors/Galera-Monitor.md#root_node_as_master)
- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

##### [set_donor_nodes](../Monitors/Galera-Monitor.md#set_donor_nodes)
- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

##### [use_priority](../Monitors/Galera-Monitor.md#use_priority)
- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes



### [MariaDB-Monitor](../Monitors/MariaDB-Monitor.md)
#### Settings
##### [assume_unique_hostnames](../Monitors/MariaDB-Monitor.md#assume_unique_hostnames)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [cooperative_monitoring_locks](../Monitors/MariaDB-Monitor.md#cooperative_monitoring_locks)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `none`, `majority_of_all`, `majority_of_running`
- **Default**: `none`

##### [enforce_read_only_servers](../Monitors/MariaDB-Monitor.md#enforce_read_only_servers)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [enforce_read_only_slaves](../Monitors/MariaDB-Monitor.md#enforce_read_only_slaves)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [enforce_writable_master](../Monitors/MariaDB-Monitor.md#enforce_writable_master)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [failcount](../Monitors/MariaDB-Monitor.md#failcount)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `5`

##### [maintenance_on_low_disk_space](../Monitors/MariaDB-Monitor.md#maintenance_on_low_disk_space)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [master_conditions](../Monitors/MariaDB-Monitor.md#master_conditions)
- **Type**: [enum_mask](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `none`, `connecting_slave`, `connected_slave`, `running_slave`, `primary_monitor_master`, `disk_space_ok`
- **Default**: `primary_monitor_master, disk_space_ok`

##### [script_max_replication_lag](../Monitors/MariaDB-Monitor.md#script_max_replication_lag)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `-1`

##### [slave_conditions](../Monitors/MariaDB-Monitor.md#slave_conditions)
- **Type**: [enum_mask](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `none`, `linked_master`, `running_master`, `writable_master`, `primary_monitor_master`
- **Default**: `none`


#### Settings for Backup operations
##### [backup_storage_address](../Monitors/MariaDB-Monitor.md#backup_storage_address)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [backup_storage_path](../Monitors/MariaDB-Monitor.md#backup_storage_path)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [rebuild_port](../Monitors/MariaDB-Monitor.md#rebuild_port)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `4444`

##### [ssh_check_host_key](../Monitors/MariaDB-Monitor.md#ssh_check_host_key)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [ssh_keyfile](../Monitors/MariaDB-Monitor.md#ssh_keyfile)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [ssh_port](../Monitors/MariaDB-Monitor.md#ssh_port)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `22`

##### [ssh_timeout](../Monitors/MariaDB-Monitor.md#ssh_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `10s`

##### [ssh_user](../Monitors/MariaDB-Monitor.md#ssh_user)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None


#### Settings for Cluster manipulation operations
##### [auto_failover](../Monitors/MariaDB-Monitor.md#auto_failover)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `true`, `on`, `yes`, `1`, `false`, `off`, `no`, `0`, `safe`
- **Default**: `false`

##### [auto_rejoin](../Monitors/MariaDB-Monitor.md#auto_rejoin)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [demotion_sql_file](../Monitors/MariaDB-Monitor.md#demotion_sql_file)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [enforce_simple_topology](../Monitors/MariaDB-Monitor.md#enforce_simple_topology)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [failover_timeout](../Monitors/MariaDB-Monitor.md#failover_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `90s`

##### [handle_events](../Monitors/MariaDB-Monitor.md#handle_events)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [master_failure_timeout](../Monitors/MariaDB-Monitor.md#master_failure_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `10s`

##### [promotion_sql_file](../Monitors/MariaDB-Monitor.md#promotion_sql_file)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [replication_master_ssl](../Monitors/MariaDB-Monitor.md#replication_master_ssl)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [replication_password](../Monitors/MariaDB-Monitor.md#replication_password)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [replication_user](../Monitors/MariaDB-Monitor.md#replication_user)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [servers_no_promotion](../Monitors/MariaDB-Monitor.md#servers_no_promotion)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [switchover_on_low_disk_space](../Monitors/MariaDB-Monitor.md#switchover_on_low_disk_space)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [switchover_timeout](../Monitors/MariaDB-Monitor.md#switchover_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `90s`

##### [verify_master_failure](../Monitors/MariaDB-Monitor.md#verify_master_failure)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`


#### Settings for Primary server write test
##### [write_test_fail_action](../Monitors/MariaDB-Monitor.md#write_test_fail_action)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Default**: `log`
- **Values**: `log`, `failover`
- **Dynamic**: Yes

##### [write_test_interval](../Monitors/MariaDB-Monitor.md#write_test_interval)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Dynamic**: Yes
- **Default**: 0s

##### [write_test_table](../Monitors/MariaDB-Monitor.md#write_test_table)
- **Type**: string
- **Dynamic**: Yes
- **Default**: `mxs.maxscale_write_test`



### [Monitor-Common](../Monitors/Monitor-Common.md)
#### Settings
##### [backend_connect_attempts](../Monitors/Monitor-Common.md#backend_connect_attempts)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `1`

##### [backend_connect_timeout](../Monitors/Monitor-Common.md#backend_connect_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `3s`

##### [backend_read_timeout](../Monitors/Monitor-Common.md#backend_read_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `3s`

##### [backend_write_timeout](../Monitors/Monitor-Common.md#backend_write_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `3s`

##### [disk_space_check_interval](../Monitors/Monitor-Common.md#disk_space_check_interval)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0s`

##### [disk_space_threshold](../Monitors/Monitor-Common.md#disk_space_threshold)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [events](../Monitors/Monitor-Common.md#events)
- **Type**: enum
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `master_down`, `master_up`, `slave_down`, `slave_up`, `server_down`, `server_up`, `lost_master`, `lost_slave`, `new_master`, `new_slave`
- **Default**: All events

##### [journal_max_age](../Monitors/Monitor-Common.md#journal_max_age)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `28800s`

##### [module](../Monitors/Monitor-Common.md#module)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: No

##### [monitor_interval](../Monitors/Monitor-Common.md#monitor_interval)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `2s`

##### [password](../Monitors/Monitor-Common.md#password)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [script](../Monitors/Monitor-Common.md#script)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: None

##### [script_timeout](../Monitors/Monitor-Common.md#script_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `90s`

##### [servers](../Monitors/Monitor-Common.md#servers)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [user](../Monitors/Monitor-Common.md#user)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes




## Protocols
### [MariaDB](../Protocols/MariaDB.md)
#### Settings
##### [allow_replication](../Protocols/MariaDB.md#allow_replication)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true



### [NoSQL](../Protocols/NoSQL.md)
#### Settings
##### [authentication_db](../Protocols/NoSQL.md#authentication_db)
- **Type**: string
- **Mandatory**: No
- **Default**: `"NoSQL"`

##### [authentication_key_id](../Protocols/NoSQL.md#authentication_key_id)
- **Type**: string
- **Mandatory**: No
- **Default**: `""`

##### [authentication_password](../Protocols/NoSQL.md#authentication_password)
- **Type**: string
- **Mandatory**: No
- **Default**: `""`

##### [authentication_required](../Protocols/NoSQL.md#authentication_required)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Default**: `false`

##### [authentication_shared](../Protocols/NoSQL.md#authentication_shared)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Default**: `false`

##### [authentication_user](../Protocols/NoSQL.md#authentication_user)
- **Type**: string
- **Mandatory**: Yes, **if** `authentication_shared` is true.

##### [authorization_enabled](../Protocols/NoSQL.md#authorization_enabled)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Default**: `false`

##### [auto_create_databases](../Protocols/NoSQL.md#auto_create_databases)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Default**: `true`

##### [auto_create_tables](../Protocols/NoSQL.md#auto_create_tables)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Default**: `true`

##### [cursor_timeout](../Protocols/NoSQL.md#cursor_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Default**: `60s`

##### [debug](../Protocols/NoSQL.md#debug)
- **Type**: [enum_mask](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Values**: `none`, `in`, `out`, `back`
- **Default**: `none`

##### [host](../Protocols/NoSQL.md#host)
- **Type**: string
- **Mandatory**: No
- **Default**: `"%"`

##### [id_length](../Protocols/NoSQL.md#id_length)
- **Type**: count
- **Mandatory**: No
- **Range**: `[35, 2048]`
- **Default*: `35`

##### [internal_cache](../Protocols/NoSQL.md#internal_cache)
- **Type**: string
- **Mandatory**: No
- **Default**: ''

##### [log_unknown_command](../Protocols/NoSQL.md#log_unknown_command)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Default**: `false`

##### [on_unknown_command](../Protocols/NoSQL.md#on_unknown_command)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Values**: `return_error`, `return_empty`
- **Default**: `return_error`

##### [ordered_insert_behavior](../Protocols/NoSQL.md#ordered_insert_behavior)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Values**: `atomic`, `default`
- **Default**: `default`

##### [password](../Protocols/NoSQL.md#password)
- **Type**: string
- **Mandatory**: No
- **Default**: `""`

##### [user](../Protocols/NoSQL.md#user)
- **Type**: string
- **Mandatory**: No
- **Default**: `""`




## Routers
### [Avrorouter](../Routers/Avrorouter.md)
#### Settings
##### [avrodir](../Routers/Avrorouter.md#avrodir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `/var/lib/maxscale/`

##### [binlogdir](../Routers/Avrorouter.md#binlogdir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `/var/lib/maxscale/`

##### [codec](../Routers/Avrorouter.md#codec)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `null`, `deflate`
- **Default**: `null`

##### [cooperative_replication](../Routers/Avrorouter.md#cooperative_replication)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `false`

##### [exclude](../Routers/Avrorouter.md#exclude)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [filestem](../Routers/Avrorouter.md#filestem)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `mysql-bin`

##### [gtid_start_pos](../Routers/Avrorouter.md#gtid_start_pos)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [match](../Routers/Avrorouter.md#match)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [server_id](../Routers/Avrorouter.md#server_id)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `1234`

##### [start_index](../Routers/Avrorouter.md#start_index)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `1`


#### Settings for Avro File
##### [block_size](../Routers/Avrorouter.md#block_size)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `16KiB`

##### [group_rows](../Routers/Avrorouter.md#group_rows)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `1000`

##### [group_trx](../Routers/Avrorouter.md#group_trx)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `1`

##### [max_data_age](../Routers/Avrorouter.md#max_data_age)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: 0s

##### [max_file_size](../Routers/Avrorouter.md#max_file_size)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: 0



### [Binlogrouter](../Routers/Binlogrouter.md)
#### Settings
##### [archivedir](../Routers/Binlogrouter.md#archivedir)
- **Type**: string
- **Mandatory**: Yes
- **Default**: No
- **Dynamic**: No

##### [compression_algorithm](../Routers/Binlogrouter.md#compression_algorithm)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `none`, `zstandard`
- **Default**: `none`

##### [datadir](../Routers/Binlogrouter.md#datadir)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `/var/lib/maxscale/binlogs`

##### [ddl_only](../Routers/Binlogrouter.md#ddl_only)
- **Type**: boolean
- **Mandatory**: No
- **Dynamic**: No
- **Default**: false

##### [encryption_cipher](../Routers/Binlogrouter.md#encryption_cipher)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `AES_CBC`, `AES_CTR`, `AES_GCM`
- **Default**: `AES_GCM`

##### [encryption_key_id](../Routers/Binlogrouter.md#encryption_key_id)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [expiration_mode](../Routers/Binlogrouter.md#expiration_mode)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Dynamic**: No
- **Values**: `purge`, `archive`
- **Default**: `purge`

##### [expire_log_duration](../Routers/Binlogrouter.md#expire_log_duration)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0s`

##### [expire_log_minimum_files](../Routers/Binlogrouter.md#expire_log_minimum_files)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `2`

##### [net_timeout](../Routers/Binlogrouter.md#net_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `10s`

##### [number_of_noncompressed_files](../Routers/Binlogrouter.md#number_of_noncompressed_files)
- **Type**: count
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `2`

##### [rpl_semi_sync_slave_enabled](../Routers/Binlogrouter.md#rpl_semi_sync_slave_enabled)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Default**: false
- **Dynamic**: Yes

##### [select_master](../Routers/Binlogrouter.md#select_master)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `false`

##### [server_id](../Routers/Binlogrouter.md#server_id)
- **Type**: count
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `1234`



### [Diff](../Routers/Diff.md)
#### Settings
##### [explain](../Routers/Diff.md#explain)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `none`, `other`, `both'
- **Default**: `both`

##### [explain_entries](../Routers/Diff.md#explain_entries)
- **Type**: non-negative integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 2

##### [explain_period](../Routers/Diff.md#explain_period)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#duration)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 15m

##### [main](../Routers/Diff.md#main)
- **Type**: server
- **Mandatory**: Yes
- **Dynamic**: No

##### [max_request_lag](../Routers/Diff.md#max_request_lag)
- **Type**: non-negative integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 10

##### [on_error](../Routers/Diff.md#on_error)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `close`, `ignore`
- **Default**: `ignore`

##### [percentile](../Routers/Diff.md#percentile)
- **Type**: count
- **Mandatory**: No
- **Dynamic**: Yes
- **Min**: 1
- **Max**: 100
- **Default**: 99

##### [qps_window](../Routers/Diff.md#qps_window)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#duration)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: 15m

##### [report](../Routers/Diff.md#report)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `always`, `on_discrepancy`, `never`
- **Default**: `on_discrepancy`

##### [reset_replication](../Routers/Diff.md#reset_replication)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `true`

##### [retain_faster_statements](../Routers/Diff.md#retain_faster_statements)
- **Type**: non-negative integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 5

##### [retain_slower_statements](../Routers/Diff.md#retain_slower_statements)
- **Type**: non-negative integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 5

##### [samples](../Routers/Diff.md#samples)
- **Type**: count
- **Mandatory**: No
- **Dynamic**: Yes
- **Min**: 100
- **Default**: 1000

##### [service](../Routers/Diff.md#service)
- **Type**: service
- **Mandatory**: Yes
- **Dynamic**: No



### [KafkaCDC](../Routers/KafkaCDC.md)
#### Settings
##### [bootstrap_servers](../Routers/KafkaCDC.md#bootstrap_servers)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: No

##### [cooperative_replication](../Routers/KafkaCDC.md#cooperative_replication)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `false`

##### [enable_idempotence](../Routers/KafkaCDC.md#enable_idempotence)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `false`

##### [exclude](../Routers/KafkaCDC.md#exclude)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [gtid](../Routers/KafkaCDC.md#gtid)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [kafka_sasl_mechanism](../Routers/KafkaCDC.md#kafka_sasl_mechanism)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `PLAIN`, `SCRAM-SHA-256`, `SCRAM-SHA-512`
- **Default**: `PLAIN`

##### [kafka_sasl_password](../Routers/KafkaCDC.md#kafka_sasl_password)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [kafka_sasl_user](../Routers/KafkaCDC.md#kafka_sasl_user)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [kafka_ssl](../Routers/KafkaCDC.md#kafka_ssl)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `false`

##### [kafka_ssl_ca](../Routers/KafkaCDC.md#kafka_ssl_ca)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [kafka_ssl_cert](../Routers/KafkaCDC.md#kafka_ssl_cert)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [kafka_ssl_key](../Routers/KafkaCDC.md#kafka_ssl_key)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [match](../Routers/KafkaCDC.md#match)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [read_gtid_from_kafka](../Routers/KafkaCDC.md#read_gtid_from_kafka)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `true`

##### [send_schema](../Routers/KafkaCDC.md#send_schema)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true

##### [server_id](../Routers/KafkaCDC.md#server_id)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `1234`

##### [timeout](../Routers/KafkaCDC.md#timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `10s`

##### [topic](../Routers/KafkaCDC.md#topic)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: No



### [KafkaImporter](../Routers/KafkaImporter.md)
#### Settings
##### [batch_size](../Routers/KafkaImporter.md#batch_size)
- **Type**: count
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `100`

##### [bootstrap_servers](../Routers/KafkaImporter.md#bootstrap_servers)
- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [engine](../Routers/KafkaImporter.md#engine)
- **Type**: string
- **Default**: `InnoDB`
- **Mandatory**: No
- **Dynamic**: Yes

##### [kafka_sasl_mechanism](../Routers/KafkaImporter.md#kafka_sasl_mechanism)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `PLAIN`, `SCRAM-SHA-256`, `SCRAM-SHA-512`
- **Default**: `PLAIN`

##### [kafka_sasl_password](../Routers/KafkaImporter.md#kafka_sasl_password)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [kafka_sasl_user](../Routers/KafkaImporter.md#kafka_sasl_user)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [kafka_ssl](../Routers/KafkaImporter.md#kafka_ssl)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `false`

##### [kafka_ssl_ca](../Routers/KafkaImporter.md#kafka_ssl_ca)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [kafka_ssl_cert](../Routers/KafkaImporter.md#kafka_ssl_cert)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [kafka_ssl_key](../Routers/KafkaImporter.md#kafka_ssl_key)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [table_name_in](../Routers/KafkaImporter.md#table_name_in)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `topic`, `key`
- **Default**: `topic`

##### [timeout](../Routers/KafkaImporter.md#timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `5000ms`

##### [topics](../Routers/KafkaImporter.md#topics)
- **Type**: stringlist
- **Mandatory**: Yes
- **Dynamic**: Yes



### [Mirror](../Routers/Mirror.md)
#### Settings
##### [exporter](../Routers/Mirror.md#exporter)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: Yes
- **Dynamic**: Yes
- **Values**: `log`, `file`, `kafka`

##### [file](../Routers/Mirror.md#file)
- **Type**: string
- **Default**: No default value
- **Mandatory**: No
- **Dynamic**: Yes

##### [kafka_broker](../Routers/Mirror.md#kafka_broker)
- **Type**: string
- **Default**: No default value
- **Mandatory**: No
- **Dynamic**: Yes

##### [kafka_topic](../Routers/Mirror.md#kafka_topic)
- **Type**: string
- **Default**: No default value
- **Mandatory**: No
- **Dynamic**: Yes

##### [main](../Routers/Mirror.md#main)
- **Type**: target
- **Mandatory**: Yes
- **Dynamic**: Yes

##### [on_error](../Routers/Mirror.md#on_error)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Default**: `ignore`
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `ignore`, `close`

##### [report](../Routers/Mirror.md#report)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Default**: `always`
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `always`, `on_conflict`



### [ReadConnRoute](../Routers/ReadConnRoute.md)
#### Settings
##### [master_accept_reads](../Routers/ReadConnRoute.md#master_accept_reads)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true

##### [max_replication_lag](../Routers/ReadConnRoute.md#max_replication_lag)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 0s

##### [router_options](../Routers/ReadConnRoute.md#router_options)
- **Type**: [enum_mask](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `master`, `slave`, `synced`, `running`
- **Default**: `running`



### [ReadWriteSplit](../Routers/ReadWriteSplit.md)
#### Settings
##### [causal_reads](../Routers/ReadWriteSplit.md#causal_reads)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `none`, `local`, `global`, `fast`, `fast_global`, `universal`, `fast_universal`
- **Default**: `none`

##### [causal_reads_timeout](../Routers/ReadWriteSplit.md#causal_reads_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 10s

##### [delayed_retry](../Routers/ReadWriteSplit.md#delayed_retry)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [delayed_retry_timeout](../Routers/ReadWriteSplit.md#delayed_retry_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 10s

##### [lazy_connect](../Routers/ReadWriteSplit.md#lazy_connect)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [master_accept_reads](../Routers/ReadWriteSplit.md#master_accept_reads)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [master_failure_mode](../Routers/ReadWriteSplit.md#master_failure_mode)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `fail_instantly`, `fail_on_write`, `error_on_write`
- **Default**: `fail_on_write` (MaxScale 23.08: `fail_instantly`)

##### [master_reconnection](../Routers/ReadWriteSplit.md#master_reconnection)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true (>= MaxScale 24.02), false(<= MaxScale 23.08)

##### [max_replication_lag](../Routers/ReadWriteSplit.md#max_replication_lag)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 0s

##### [max_slave_connections](../Routers/ReadWriteSplit.md#max_slave_connections)
- **Type**: integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 255

##### [retry_failed_reads](../Routers/ReadWriteSplit.md#retry_failed_reads)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true

##### [slave_connections](../Routers/ReadWriteSplit.md#slave_connections)
- **Type**: integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 255

##### [slave_selection_criteria](../Routers/ReadWriteSplit.md#slave_selection_criteria)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `least_current_operations`, `adaptive_routing`, `least_behind_master`, `least_router_connections`, `least_global_connections`
- **Default**: `least_current_operations`

##### [strict_multi_stmt](../Routers/ReadWriteSplit.md#strict_multi_stmt)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [strict_sp_calls](../Routers/ReadWriteSplit.md#strict_sp_calls)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [strict_tmp_tables](../Routers/ReadWriteSplit.md#strict_tmp_tables)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true (>= MaxScale 24.02), false (<= MaxScale 23.08)

##### [transaction_replay](../Routers/ReadWriteSplit.md#transaction_replay)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [transaction_replay_attempts](../Routers/ReadWriteSplit.md#transaction_replay_attempts)
- **Type**: integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 5

##### [transaction_replay_checksum](../Routers/ReadWriteSplit.md#transaction_replay_checksum)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `full`, `result_only`, `no_insert_id`
- **Default**: `full`

##### [transaction_replay_max_size](../Routers/ReadWriteSplit.md#transaction_replay_max_size)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 1 MiB

##### [transaction_replay_retry_on_deadlock](../Routers/ReadWriteSplit.md#transaction_replay_retry_on_deadlock)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [transaction_replay_retry_on_mismatch](../Routers/ReadWriteSplit.md#transaction_replay_retry_on_mismatch)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [transaction_replay_safe_commit](../Routers/ReadWriteSplit.md#transaction_replay_safe_commit)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true

##### [transaction_replay_timeout](../Routers/ReadWriteSplit.md#transaction_replay_timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 30s (>= MaxScale 24.02), 0s (<= MaxScale 23.08)

##### [use_sql_variables_in](../Routers/ReadWriteSplit.md#use_sql_variables_in)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `master`, `all`
- **Default**: `all`



### [SchemaRouter](../Routers/SchemaRouter.md)
#### Settings
##### [allow_duplicates](../Routers/SchemaRouter.md#allow_duplicates)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

##### [ignore_tables](../Routers/SchemaRouter.md#ignore_tables)
- **Type**: stringlist
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `""`

##### [ignore_tables_regex](../Routers/SchemaRouter.md#ignore_tables_regex)
- **Type**: [regex](../Getting-Started/Configuration-Guide.md#regular-expressions)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

##### [max_staleness](../Routers/SchemaRouter.md#max_staleness)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 150s

##### [refresh_databases](../Routers/SchemaRouter.md#refresh_databases)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `false`

##### [refresh_interval](../Routers/SchemaRouter.md#refresh_interval)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `300s`



### [SmartRouter](../Routers/SmartRouter.md)
#### Settings
##### [master](../Routers/SmartRouter.md#master)
- **Type**: target
- **Mandatory**: Yes
- **Dynamic**: No




