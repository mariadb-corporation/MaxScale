/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * Common configuration parameters names
 *
 * All of the constants resolve to a lowercase version without the CN_ prefix.
 * For example CN_PASSWORD resolves to the static string "password".
 */
constexpr char CN_ACCOUNT[] = "account";
constexpr char CN_ADDRESS[] = "address";
constexpr char CN_ADMIN_AUTH[] = "admin_auth";
constexpr char CN_ADMIN_ENABLED[] = "admin_enabled";
constexpr char CN_ADMIN_HOST[] = "admin_host";
constexpr char CN_ADMIN_LOG_AUTH_FAILURES[] = "admin_log_auth_failures";
constexpr char CN_ADMIN_PORT[] = "admin_port";
constexpr char CN_ADMIN_SSL_CA_CERT[] = "admin_ssl_ca_cert";
constexpr char CN_ADMIN_SSL_CERT[] = "admin_ssl_cert";
constexpr char CN_ADMIN_SSL_KEY[] = "admin_ssl_key";
constexpr char CN_ARGUMENTS[] = "arguments";
constexpr char CN_ARG_MAX[] = "arg_max";
constexpr char CN_ARG_MIN[] = "arg_min";
constexpr char CN_ATTRIBUTES[] = "attributes";
constexpr char CN_AUTHENTICATOR[] = "authenticator";
constexpr char CN_AUTHENTICATOR_DIAGNOSTICS[] = "authenticator_diagnostics";
constexpr char CN_AUTHENTICATOR_OPTIONS[] = "authenticator_options";
constexpr char CN_AUTH_ALL_SERVERS[] = "auth_all_servers";
constexpr char CN_AUTH_CONNECT_TIMEOUT[] = "auth_connect_timeout";
constexpr char CN_AUTH_READ_TIMEOUT[] = "auth_read_timeout";
constexpr char CN_AUTH_WRITE_TIMEOUT[] = "auth_write_timeout";
constexpr char CN_AUTO[] = "auto";
constexpr char CN_CACHE[] = "cache";
constexpr char CN_CACHE_SIZE[] = "cache_size";
constexpr char CN_CLASSIFICATION[] = "classification";
constexpr char CN_CLASSIFY[] = "classify";
constexpr char CN_CLUSTER[] = "cluster";
constexpr char CN_CONNECTION_TIMEOUT[] = "connection_timeout";
constexpr char CN_NET_WRITE_TIMEOUT[] = "net_write_timeout";
constexpr char CN_DATA[] = "data";
constexpr char CN_DEFAULT[] = "default";
constexpr char CN_DESCRIPTION[] = "description";
constexpr char CN_DISK_SPACE_THRESHOLD[] = "disk_space_threshold";
constexpr char CN_DUMP_LAST_STATEMENTS[] = "dump_last_statements";
constexpr char CN_ENABLE_ROOT_USER[] = "enable_root_user";
constexpr char CN_EXTRA_PORT[] = "extra_port";
constexpr char CN_FIELDS[] = "fields";
constexpr char CN_FILTERS[] = "filters";
constexpr char CN_FILTER[] = "filter";
constexpr char CN_FILTER_DIAGNOSTICS[] = "filter_diagnostics";
constexpr char CN_FORCE[] = "force";
constexpr char CN_FUNCTIONS[] = "functions";
constexpr char CN_GATEWAY[] = "gateway";
constexpr char CN_HAS_WHERE_CLAUSE[] = "has_where_clause";
constexpr char CN_HITS[] = "hits";
constexpr char CN_ID[] = "id";
constexpr char CN_INET[] = "inet";
constexpr char CN_LINKS[] = "links";
constexpr char CN_LISTENERS[] = "listeners";
constexpr char CN_LISTENER[] = "listener";
constexpr char CN_LOAD_PERSISTED_CONFIGS[] = "load_persisted_configs";
constexpr char CN_LOCALHOST_MATCH_WILDCARD_HOST[] = "localhost_match_wildcard_host";
constexpr char CN_LOG_AUTH_WARNINGS[] = "log_auth_warnings";
constexpr char CN_LOG_THROTTLING[] = "log_throttling";
constexpr char CN_MAX_AUTH_ERRORS_UNTIL_BLOCK[] = "max_auth_errors_until_block";
constexpr char CN_MAXSCALE[] = "maxscale";
constexpr char CN_MAX_CONNECTIONS[] = "max_connections";
constexpr char CN_MAX_RETRY_INTERVAL[] = "max_retry_interval";
constexpr char CN_META[] = "meta";
constexpr char CN_METHOD[] = "method";
constexpr char CN_MODULES[] = "modules";
constexpr char CN_MODULE[] = "module";
constexpr char CN_MODULE_COMMAND[] = "module_command";
constexpr char CN_MONITORS[] = "monitors";
constexpr char CN_MONITOR[] = "monitor";
constexpr char CN_MONITOR_DIAGNOSTICS[] = "monitor_diagnostics";
constexpr char CN_MS_TIMESTAMP[] = "ms_timestamp";
constexpr char CN_NAME[] = "name";
constexpr char CN_NON_BLOCKING_POLLS[] = "non_blocking_polls";
constexpr char CN_OPERATION[] = "operation";
constexpr char CN_OPTIONS[] = "options";
constexpr char CN_PARAMETERS[] = "parameters";
constexpr char CN_PARSE_RESULT[] = "parse_result";
constexpr char CN_PASSIVE[] = "passive";
constexpr char CN_PASSWORD[] = "password";
constexpr char CN_POLL_SLEEP[] = "poll_sleep";
constexpr char CN_PORT[] = "port";
constexpr char CN_PROTOCOL[] = "protocol";
constexpr char CN_QUERY_CLASSIFIER[] = "query_classifier";
constexpr char CN_QUERY_CLASSIFIER_ARGS[] = "query_classifier_args";
constexpr char CN_QUERY_CLASSIFIER_CACHE_SIZE[] = "query_classifier_cache_size";
constexpr char CN_QUERY_RETRIES[] = "query_retries";
constexpr char CN_QUERY_RETRY_TIMEOUT[] = "query_retry_timeout";
constexpr char CN_RANK[] = "rank";
constexpr char CN_RELATIONSHIPS[] = "relationships";
constexpr char CN_REQUIRED[] = "required";
constexpr char CN_RETAIN_LAST_STATEMENTS[] = "retain_last_statements";
constexpr char CN_RETRY_ON_FAILURE[] = "retry_on_failure";
constexpr char CN_ROUTER[] = "router";
constexpr char CN_ROUTER_DIAGNOSTICS[] = "router_diagnostics";
constexpr char CN_ROUTER_OPTIONS[] = "router_options";
constexpr char CN_SELF[] = "self";
constexpr char CN_SERVERS[] = "servers";
constexpr char CN_SERVER[] = "server";
constexpr char CN_SERVICES[] = "services";
constexpr char CN_SERVICE[] = "service";
constexpr char CN_SESSIONS[] = "sessions";
constexpr char CN_SESSION_TRACE[] = "session_trace";
constexpr char CN_SESSION_TRACK_TRX_STATE[] = "session_track_trx_state";
constexpr char CN_SKIP_PERMISSION_CHECKS[] = "skip_permission_checks";
constexpr char CN_SOCKET[] = "socket";
constexpr char CN_SQL_MODE[] = "sql_mode";
constexpr char CN_SSL[] = "ssl";
constexpr char CN_SSL_CA_CERT[] = "ssl_ca_cert";
constexpr char CN_SSL_CERT[] = "ssl_cert";
constexpr char CN_SSL_CERT_VERIFY_DEPTH[] = "ssl_cert_verify_depth";
constexpr char CN_SSL_KEY[] = "ssl_key";
constexpr char CN_SSL_VERIFY_PEER_CERTIFICATE[] = "ssl_verify_peer_certificate";
constexpr char CN_SSL_VERSION[] = "ssl_version";
constexpr char CN_STATEMENTS[] = "statements";
constexpr char CN_STATEMENT[] = "statement";
constexpr char CN_STATE[] = "state";
constexpr char CN_STRIP_DB_ESC[] = "strip_db_esc";
constexpr char CN_SUBSTITUTE_VARIABLES[] = "substitute_variables";
constexpr char CN_TARGETS[] = "targets";
constexpr char CN_THREADS[] = "threads";
constexpr char CN_THREAD_STACK_SIZE[] = "thread_stack_size";
constexpr char CN_TICKS[] = "ticks";
constexpr char CN_TYPE[] = "type";
constexpr char CN_TYPE_MASK[] = "type_mask";
constexpr char CN_UNIX[] = "unix";
constexpr char CN_USERS[] = "users";
constexpr char CN_USER[] = "user";
constexpr char CN_VERSION_STRING[] = "version_string";
constexpr char CN_WEIGHTBY[] = "weightby";
constexpr char CN_WRITEQ_HIGH_WATER[] = "writeq_high_water";
constexpr char CN_WRITEQ_LOW_WATER[] = "writeq_low_water";
constexpr char CN_YES[] = "yes";

/*
 * Global configuration items that are read (or pre_parsed) to be available for
 * subsequent configuration reading. @see config_pre_parse_global_params.
 */
constexpr char CN_LOGDIR[] = "logdir";
constexpr char CN_LIBDIR[] = "libdir";
constexpr char CN_PIDDIR[] = "piddir";
constexpr char CN_DATADIR[] = "datadir";
constexpr char CN_CACHEDIR[] = "cachedir";
constexpr char CN_LANGUAGE[] = "language";
constexpr char CN_EXECDIR[] = "execdir";
constexpr char CN_CONNECTOR_PLUGINDIR[] = "connector_plugindir";
constexpr char CN_PERSISTDIR[] = "persistdir";
constexpr char CN_MODULE_CONFIGDIR[] = "module_configdir";
constexpr char CN_SYSLOG[] = "syslog";
constexpr char CN_MAXLOG[] = "maxlog";
constexpr char CN_LOG_AUGMENTATION[] = "log_augmentation";
