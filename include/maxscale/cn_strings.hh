/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
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
constexpr char CN_ADMIN_LOG_AUTH_FAILURES[] = "admin_log_auth_failures";
constexpr char CN_ATTRIBUTES[] = "attributes";
constexpr char CN_AUTHENTICATOR[] = "authenticator";
constexpr char CN_AUTHENTICATOR_OPTIONS[] = "authenticator_options";
constexpr char CN_AUTH_CONNECT_TIMEOUT[] = "auth_connect_timeout";
constexpr char CN_AUTH_READ_TIMEOUT[] = "auth_read_timeout";
constexpr char CN_AUTH_WRITE_TIMEOUT[] = "auth_write_timeout";
constexpr char CN_CLUSTER[] = "cluster";
constexpr char CN_CONFIG_SYNC_CLUSTER[] = "config_sync_cluster";
constexpr char CN_CONFIG_SYNC_USER[] = "config_sync_user";
constexpr char CN_CONFIG_SYNC_PASSWORD[] = "config_sync_password";
constexpr char CN_CONFIG_SYNC_TIMEOUT[] = "config_sync_timeout";
constexpr char CN_CONFIG_SYNC_INTERVAL[] = "config_sync_interval";
constexpr char CN_DATA[] = "data";
constexpr char CN_DESCRIPTION[] = "description";
constexpr char CN_DISK_SPACE_THRESHOLD[] = "disk_space_threshold";
constexpr char CN_ENABLE_ROOT_USER[] = "enable_root_user";
constexpr char CN_FILTERS[] = "filters";
constexpr char CN_FILTER[] = "filter";
constexpr char CN_ID[] = "id";
constexpr char CN_INET[] = "inet";
constexpr char CN_LINKS[] = "links";
constexpr char CN_LISTENERS[] = "listeners";
constexpr char CN_LISTENER[] = "listener";
constexpr char CN_MAXSCALE[] = "maxscale";
constexpr char CN_MODULE[] = "module";
constexpr char CN_MONITORS[] = "monitors";
constexpr char CN_MONITOR[] = "monitor";
constexpr char CN_NAME[] = "name";
constexpr char CN_PARAMETERS[] = "parameters";
constexpr char CN_PASSWORD[] = "password";
constexpr char CN_PORT[] = "port";
constexpr char CN_PROTOCOL[] = "protocol";
constexpr char CN_QUERY_CLASSIFIER[] = "query_classifier";
constexpr char CN_QUERY_CLASSIFIER_CACHE_SIZE[] = "query_classifier_cache_size";
constexpr char CN_RANK[] = "rank";
constexpr char CN_REBALANCE_THRESHOLD[] = "rebalance_threshold";
constexpr char CN_RELATIONSHIPS[] = "relationships";
constexpr char CN_REQUIRED[] = "required";
constexpr char CN_RETAIN_LAST_STATEMENTS[] = "retain_last_statements";
constexpr char CN_ROUTER[] = "router";
constexpr char CN_SERVERS[] = "servers";
constexpr char CN_SERVICES[] = "services";
constexpr char CN_SERVICE[] = "service";
constexpr char CN_SESSION_TRACE[] = "session_trace";
constexpr char CN_SOCKET[] = "socket";
constexpr char CN_SQL_MODE[] = "sql_mode";
constexpr char CN_SSL[] = "ssl";
constexpr char CN_SSL_CA_CERT[] = "ssl_ca_cert";
constexpr char CN_SSL_CERT[] = "ssl_cert";
constexpr char CN_SSL_CERT_VERIFY_DEPTH[] = "ssl_cert_verify_depth";
constexpr char CN_SSL_CIPHER[] = "ssl_cipher";
constexpr char CN_SSL_CRL[] = "ssl_crl";
constexpr char CN_SSL_KEY[] = "ssl_key";
constexpr char CN_SSL_VERIFY_PEER_CERTIFICATE[] = "ssl_verify_peer_certificate";
constexpr char CN_SSL_VERIFY_PEER_HOST[] = "ssl_verify_peer_host";
constexpr char CN_SSL_VERSION[] = "ssl_version";
constexpr char CN_STATE[] = "state";
constexpr char CN_SUBSTITUTE_VARIABLES[] = "substitute_variables";
constexpr char CN_TARGETS[] = "targets";
constexpr char CN_THREADS[] = "threads";
constexpr char CN_TYPE[] = "type";
constexpr char CN_UNIX[] = "unix";
constexpr char CN_USER[] = "user";
constexpr char CN_VERSION_STRING[] = "version_string";

/*
 * Global configuration items that are read (or pre_parsed) to be available for
 * subsequent configuration reading. @see config_pre_parse_global_params.
 */
constexpr char CN_CACHEDIR[] = "cachedir";
constexpr char CN_CONNECTOR_PLUGINDIR[] = "connector_plugindir";
constexpr char CN_DATADIR[] = "datadir";
constexpr char CN_EXECDIR[] = "execdir";
constexpr char CN_LANGUAGE[] = "language";
constexpr char CN_LIBDIR[] = "libdir";
constexpr char CN_SHAREDIR[] = "sharedir";
constexpr char CN_LOGDIR[] = "logdir";
constexpr char CN_LOG_AUGMENTATION[] = "log_augmentation";
constexpr char CN_MAXLOG[] = "maxlog";
constexpr char CN_MODULE_CONFIGDIR[] = "module_configdir";
constexpr char CN_PERSISTDIR[] = "persistdir";
constexpr char CN_PIDDIR[] = "piddir";
constexpr char CN_SYSLOG[] = "syslog";
