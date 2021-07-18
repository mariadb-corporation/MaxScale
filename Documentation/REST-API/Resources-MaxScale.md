# MaxScale Resource

The MaxScale resource represents a MaxScale instance and it is the core on top
of which the modules build upon.

[TOC]

## Resource Operations

## Get global information

```
GET /v1/maxscale
```

Retrieve global information about a MaxScale instance. This includes various
file locations, configuration options and version information.

#### Response

`Status: 200 OK`

```javascript
{
    "data": {
        "attributes": {
            "activated_at": "Fri, 16 Jul 2021 06:51:10 GMT",
            "commit": "601a9b92f4feed5dd5a92f9e28a47d9162bd4901",
            "parameters": {
                "admin_auth": true,
                "admin_enabled": true,
                "admin_gui": true,
                "admin_host": "127.0.0.1",
                "admin_log_auth_failures": true,
                "admin_pam_readonly_service": null,
                "admin_pam_readwrite_service": null,
                "admin_port": 8989,
                "admin_secure_gui": true,
                "admin_ssl_ca_cert": null,
                "admin_ssl_cert": null,
                "admin_ssl_key": null,
                "admin_ssl_version": "MAX",
                "auth_connect_timeout": 10000,
                "auth_read_timeout": 10000,
                "auth_write_timeout": 10000,
                "cachedir": "/tmp/build/cache/maxscale",
                "connector_plugindir": "/tmp/build/lib64/mysql/plugin",
                "datadir": "/tmp/build/lib/maxscale",
                "debug": null,
                "dump_last_statements": "never",
                "execdir": "/tmp/build/bin",
                "language": "/tmp/build/lib/maxscale",
                "libdir": "/tmp/build/lib64/maxscale",
                "load_persisted_configs": true,
                "local_address": null,
                "log_debug": false,
                "log_info": false,
                "log_notice": true,
                "log_throttling": {
                    "count": 0,
                    "suppress": 0,
                    "window": 0
                },
                "log_warn_super_user": false,
                "log_warning": true,
                "logdir": "/tmp/build/log/maxscale",
                "max_auth_errors_until_block": 10,
                "maxlog": true,
                "module_configdir": "/tmp/build/etc/maxscale.modules.d",
                "ms_timestamp": false,
                "passive": false,
                "persistdir": "/tmp/build/lib/maxscale/maxscale.cnf.d",
                "piddir": "/tmp/build/run/maxscale",
                "query_classifier": "qc_sqlite",
                "query_classifier_args": null,
                "query_classifier_cache_size": 0,
                "query_retries": 1,
                "query_retry_timeout": 5000,
                "rebalance_period": 0,
                "rebalance_threshold": 20,
                "rebalance_window": 10,
                "retain_last_statements": 0,
                "session_trace": 0,
                "skip_permission_checks": false,
                "sql_mode": "default",
                "syslog": true,
                "threads": 8,
                "users_refresh_interval": 0,
                "users_refresh_time": 30000,
                "writeq_high_water": 16777216,
                "writeq_low_water": 8192
            },
            "process_datadir": "/tmp/build/lib/maxscale/data1328009",
            "started_at": "Fri, 16 Jul 2021 06:51:10 GMT",
            "uptime": 284,
            "version": "2.5.14"
        },
        "id": "maxscale",
        "type": "maxscale"
    },
    "links": {
        "self": "http://localhost:8989/v1/maxscale/"
    }
}
```

## Update MaxScale parameters

```
PATCH /v1/maxscale
```

Update MaxScale parameters. The request body must define updated values for the
`data.attributes.parameters` object. The following parameters can be altered:

- [admin_auth](../Getting-Started/Configuration-Guide.md#admin_auth)
- [auth_connect_timeout](../Getting-Started/Configuration-Guide.md#auth_connect_timeout)
- [auth_read_timeout](../Getting-Started/Configuration-Guide.md#auth_read_timeout)
- [auth_write_timeout](../Getting-Started/Configuration-Guide.md#auth_write_timeout)
- [admin_log_auth_failures](../Getting-Started/Configuration-Guide.md#admin_log_auth_failures)
- [passive](../Getting-Started/Configuration-Guide.md#passive)

#### Response

Parameters modified:

`Status: 204 No Content`

Invalid JSON body:

`Status: 403 Forbidden`

## Get thread information

```
GET /v1/maxscale/threads/:id
```

Get the information and statistics of a particular thread. The _:id_ in
the URI must map to a valid thread number between 0 and the configured
value of `threads`.

#### Response

`Status: 200 OK`

```javascript
{
    "data": {
        "attributes": {
            "stats": {
                "accepts": 0,
                "avg_event_queue_length": 1,
                "current_descriptors": 4,
                "errors": 0,
                "hangups": 0,
                "load": {
                    "last_hour": 0,
                    "last_minute": 0,
                    "last_second": 0
                },
                "max_event_queue_length": 1,
                "max_exec_time": 0,
                "max_queue_time": 0,
                "query_classifier_cache": {
                    "evictions": 0,
                    "hits": 0,
                    "inserts": 0,
                    "misses": 0,
                    "size": 0
                },
                "reads": 197,
                "total_descriptors": 4,
                "writes": 0
            }
        },
        "id": "0",
        "links": {
            "self": "http://localhost:8989/v1/threads/0"
        },
        "type": "threads"
    },
    "links": {
        "self": "http://localhost:8989/v1/maxscale/threads/0"
    }
}
```

## Get information for all threads

```
GET /v1/maxscale/threads
```

Get the information for all threads. Returns a collection of threads resources.

#### Response

`Status: 200 OK`

```javascript
{
    "data": [
        {
            "attributes": {
                "stats": {
                    "accepts": 0,
                    "avg_event_queue_length": 1,
                    "current_descriptors": 4,
                    "errors": 0,
                    "hangups": 0,
                    "load": {
                        "last_hour": 0,
                        "last_minute": 0,
                        "last_second": 0
                    },
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0,
                    "query_classifier_cache": {
                        "evictions": 0,
                        "hits": 0,
                        "inserts": 0,
                        "misses": 0,
                        "size": 0
                    },
                    "reads": 198,
                    "total_descriptors": 4,
                    "writes": 0
                }
            },
            "id": "0",
            "links": {
                "self": "http://localhost:8989/v1/threads/0"
            },
            "type": "threads"
        },
        {
            "attributes": {
                "stats": {
                    "accepts": 0,
                    "avg_event_queue_length": 1,
                    "current_descriptors": 4,
                    "errors": 0,
                    "hangups": 0,
                    "load": {
                        "last_hour": 0,
                        "last_minute": 0,
                        "last_second": 0
                    },
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0,
                    "query_classifier_cache": {
                        "evictions": 0,
                        "hits": 0,
                        "inserts": 0,
                        "misses": 0,
                        "size": 0
                    },
                    "reads": 192,
                    "total_descriptors": 4,
                    "writes": 0
                }
            },
            "id": "1",
            "links": {
                "self": "http://localhost:8989/v1/threads/1"
            },
            "type": "threads"
        },
        {
            "attributes": {
                "stats": {
                    "accepts": 0,
                    "avg_event_queue_length": 1,
                    "current_descriptors": 4,
                    "errors": 0,
                    "hangups": 0,
                    "load": {
                        "last_hour": 0,
                        "last_minute": 0,
                        "last_second": 0
                    },
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0,
                    "query_classifier_cache": {
                        "evictions": 0,
                        "hits": 0,
                        "inserts": 0,
                        "misses": 0,
                        "size": 0
                    },
                    "reads": 192,
                    "total_descriptors": 4,
                    "writes": 0
                }
            },
            "id": "2",
            "links": {
                "self": "http://localhost:8989/v1/threads/2"
            },
            "type": "threads"
        },
        {
            "attributes": {
                "stats": {
                    "accepts": 0,
                    "avg_event_queue_length": 1,
                    "current_descriptors": 4,
                    "errors": 0,
                    "hangups": 0,
                    "load": {
                        "last_hour": 0,
                        "last_minute": 0,
                        "last_second": 0
                    },
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0,
                    "query_classifier_cache": {
                        "evictions": 0,
                        "hits": 0,
                        "inserts": 0,
                        "misses": 0,
                        "size": 0
                    },
                    "reads": 192,
                    "total_descriptors": 4,
                    "writes": 0
                }
            },
            "id": "3",
            "links": {
                "self": "http://localhost:8989/v1/threads/3"
            },
            "type": "threads"
        },
        {
            "attributes": {
                "stats": {
                    "accepts": 0,
                    "avg_event_queue_length": 1,
                    "current_descriptors": 4,
                    "errors": 0,
                    "hangups": 0,
                    "load": {
                        "last_hour": 0,
                        "last_minute": 0,
                        "last_second": 0
                    },
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0,
                    "query_classifier_cache": {
                        "evictions": 0,
                        "hits": 0,
                        "inserts": 0,
                        "misses": 0,
                        "size": 0
                    },
                    "reads": 192,
                    "total_descriptors": 4,
                    "writes": 0
                }
            },
            "id": "4",
            "links": {
                "self": "http://localhost:8989/v1/threads/4"
            },
            "type": "threads"
        },
        {
            "attributes": {
                "stats": {
                    "accepts": 0,
                    "avg_event_queue_length": 1,
                    "current_descriptors": 4,
                    "errors": 0,
                    "hangups": 0,
                    "load": {
                        "last_hour": 0,
                        "last_minute": 0,
                        "last_second": 0
                    },
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0,
                    "query_classifier_cache": {
                        "evictions": 0,
                        "hits": 0,
                        "inserts": 0,
                        "misses": 0,
                        "size": 0
                    },
                    "reads": 192,
                    "total_descriptors": 4,
                    "writes": 0
                }
            },
            "id": "5",
            "links": {
                "self": "http://localhost:8989/v1/threads/5"
            },
            "type": "threads"
        },
        {
            "attributes": {
                "stats": {
                    "accepts": 0,
                    "avg_event_queue_length": 1,
                    "current_descriptors": 4,
                    "errors": 0,
                    "hangups": 0,
                    "load": {
                        "last_hour": 0,
                        "last_minute": 0,
                        "last_second": 0
                    },
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0,
                    "query_classifier_cache": {
                        "evictions": 0,
                        "hits": 0,
                        "inserts": 0,
                        "misses": 0,
                        "size": 0
                    },
                    "reads": 192,
                    "total_descriptors": 4,
                    "writes": 0
                }
            },
            "id": "6",
            "links": {
                "self": "http://localhost:8989/v1/threads/6"
            },
            "type": "threads"
        },
        {
            "attributes": {
                "stats": {
                    "accepts": 1,
                    "avg_event_queue_length": 1,
                    "current_descriptors": 7,
                    "errors": 0,
                    "hangups": 0,
                    "load": {
                        "last_hour": 0,
                        "last_minute": 0,
                        "last_second": 0
                    },
                    "max_event_queue_length": 2,
                    "max_exec_time": 0,
                    "max_queue_time": 0,
                    "query_classifier_cache": {
                        "evictions": 0,
                        "hits": 0,
                        "inserts": 2,
                        "misses": 2,
                        "size": 40
                    },
                    "reads": 201,
                    "total_descriptors": 7,
                    "writes": 12
                }
            },
            "id": "7",
            "links": {
                "self": "http://localhost:8989/v1/threads/7"
            },
            "type": "threads"
        }
    ],
    "links": {
        "self": "http://localhost:8989/v1/maxscale/threads/"
    }
}
```

## Get logging information

```
GET /v1/maxscale/logs
```

Get information about the current state of logging, enabled log files and the
location where the log files are stored.

#### Response

`Status: 200 OK`

```javascript
{
    "data": {
        "attributes": {
            "log_file": "/tmp/build/log/maxscale/maxscale.log",
            "log_priorities": [
                "alert",
                "error",
                "warning",
                "notice"
            ],
            "parameters": {
                "highprecision": false,
                "log_debug": false,
                "log_info": false,
                "log_notice": true,
                "log_warning": true,
                "maxlog": true,
                "syslog": false,
                "throttling": {
                    "count": 10,
                    "suppress_ms": 10000,
                    "window_ms": 1000
                }
            }
        },
        "id": "logs",
        "type": "logs"
    },
    "links": {
        "self": "http://localhost:8989/v1/maxscale/logs/"
    }
}
```

## Update logging parameters

```
PATCH /v1/maxscale/logs
```

Update logging parameters. The request body must define updated values for the
`data.attributes.parameters` object. All logging parameters can be altered at runtime.

#### Response

Parameters modified:

`Status: 204 No Content`

Invalid JSON body:

`Status: 403 Forbidden`

## Flush and rotate log files

```
POST /v1/maxscale/logs/flush
```

Flushes any pending messages to disk and reopens the log files. The body of the
message is ignored.

#### Response

`Status: 204 No Content`

## Get task schedule

```
GET /v1/maxscale/tasks
```

Retrieve all pending tasks that are queued for execution.

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/tasks/"
    },
    "data": [] // No tasks active
}
```

## Get a loaded module

```
GET /v1/maxscale/modules/:name
```

Retrieve information about a loaded module. The _:name_ must be the name of a
valid loaded module or either `maxscale` or `servers`.

The `maxscale` module will display the global configuration options
(i.e. everything under the `[maxscale]` section) as a module.

The `servers` module displays the server object type and the configuration
parameters it accepts as a module.

#### Response

`Status: 200 OK`

```javascript
{
    "data": {
        "attributes": {
            "api": "router",
            "commands": [],
            "description": "A Read/Write splitting router for enhancement read scalability",
            "maturity": "GA",
            "module_type": "Router",
            "parameters": [
                {
                    "default_value": "false",
                    "enum_values": [
                        "false",
                        "off",
                        "0",
                        "true",
                        "on",
                        "1",
                        "none",
                        "local",
                        "global",
                        "fast"
                    ],
                    "mandatory": false,
                    "name": "causal_reads",
                    "type": "enum"
                },
                {
                    "default_value": "10000ms",
                    "mandatory": false,
                    "name": "causal_reads_timeout",
                    "type": "duration",
                    "unit": "ms"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "delayed_retry",
                    "type": "bool"
                },
                {
                    "default_value": "10000ms",
                    "mandatory": false,
                    "name": "delayed_retry_timeout",
                    "type": "duration",
                    "unit": "ms"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "disable_sescmd_history",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "lazy_connect",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "master_accept_reads",
                    "type": "bool"
                },
                {
                    "default_value": "fail_instantly",
                    "enum_values": [
                        "fail_instantly",
                        "fail_on_write",
                        "error_on_write"
                    ],
                    "mandatory": false,
                    "name": "master_failure_mode",
                    "type": "enum"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "master_reconnection",
                    "type": "bool"
                },
                {
                    "default_value": 50,
                    "mandatory": false,
                    "name": "max_sescmd_history",
                    "type": "count"
                },
                {
                    "default_value": "255",
                    "mandatory": false,
                    "name": "max_slave_connections",
                    "type": "string"
                },
                {
                    "default_value": "0ms",
                    "mandatory": false,
                    "name": "max_slave_replication_lag",
                    "type": "duration",
                    "unit": "ms"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "optimistic_trx",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "prune_sescmd_history",
                    "type": "bool"
                },
                {
                    "default_value": true,
                    "mandatory": false,
                    "name": "retry_failed_reads",
                    "type": "bool"
                },
                {
                    "default_value": 255,
                    "mandatory": false,
                    "name": "slave_connections",
                    "type": "count"
                },
                {
                    "default_value": "LEAST_CURRENT_OPERATIONS",
                    "enum_values": [
                        "LEAST_GLOBAL_CONNECTIONS",
                        "LEAST_ROUTER_CONNECTIONS",
                        "LEAST_BEHIND_MASTER",
                        "LEAST_CURRENT_OPERATIONS",
                        "ADAPTIVE_ROUTING"
                    ],
                    "mandatory": false,
                    "name": "slave_selection_criteria",
                    "type": "enum"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "strict_multi_stmt",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "strict_sp_calls",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "transaction_replay",
                    "type": "bool"
                },
                {
                    "default_value": 5,
                    "mandatory": false,
                    "name": "transaction_replay_attempts",
                    "type": "count"
                },
                {
                    "default_value": 1073741824,
                    "mandatory": false,
                    "name": "transaction_replay_max_size",
                    "type": "size"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "transaction_replay_retry_on_deadlock",
                    "type": "bool"
                },
                {
                    "default_value": "all",
                    "enum_values": [
                        "all",
                        "master"
                    ],
                    "mandatory": false,
                    "name": "use_sql_variables_in",
                    "type": "enum"
                },
                {
                    "mandatory": false,
                    "name": "router_options",
                    "type": "string"
                },
                {
                    "mandatory": true,
                    "name": "user",
                    "type": "string"
                },
                {
                    "mandatory": true,
                    "name": "password",
                    "type": "password string"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "enable_root_user",
                    "type": "bool"
                },
                {
                    "default_value": 0,
                    "mandatory": false,
                    "name": "max_connections",
                    "type": "count"
                },
                {
                    "default_value": "0",
                    "mandatory": false,
                    "name": "connection_timeout",
                    "type": "duration",
                    "unit": "s"
                },
                {
                    "default_value": "0",
                    "mandatory": false,
                    "name": "net_write_timeout",
                    "type": "duration",
                    "unit": "s"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "auth_all_servers",
                    "type": "bool"
                },
                {
                    "default_value": true,
                    "mandatory": false,
                    "name": "strip_db_esc",
                    "type": "bool"
                },
                {
                    "default_value": true,
                    "mandatory": false,
                    "name": "localhost_match_wildcard_host",
                    "type": "bool"
                },
                {
                    "mandatory": false,
                    "name": "version_string",
                    "type": "string"
                },
                {
                    "default_value": true,
                    "mandatory": false,
                    "name": "log_auth_warnings",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "session_track_trx_state",
                    "type": "bool"
                },
                {
                    "default_value": -1,
                    "mandatory": false,
                    "name": "retain_last_statements",
                    "type": "int"
                },
                {
                    "default_value": false,
                    "mandatory": false,
                    "name": "session_trace",
                    "type": "bool"
                },
                {
                    "default_value": "primary",
                    "enum_values": [
                        "primary",
                        "secondary"
                    ],
                    "mandatory": false,
                    "name": "rank",
                    "type": "enum"
                },
                {
                    "default_value": "300s",
                    "mandatory": false,
                    "name": "connection_keepalive",
                    "type": "duration",
                    "unit": "s"
                }
            ],
            "version": "V1.1.0"
        },
        "id": "readwritesplit",
        "links": {
            "self": "http://localhost:8989/v1/modules/readwritesplit"
        },
        "type": "modules"
    },
    "links": {
        "self": "http://localhost:8989/v1/maxscale/modules/"
    }
}
```

## Get all loaded modules

```
GET /v1/maxscale/modules
```

Retrieve information about all loaded modules.

This endpoint supports the `load=all` parameter. When defined, all modules
located in the MaxScale module directory (`libdir`) will be loaded. This allows
one to see the parameters of a module before the object is created.

#### Response

`Status: 200 OK`

```javascript
{
    "data": [
        {
            "attributes": {
                "commands": [],
                "description": "maxscale",
                "maturity": "GA",
                "module_type": "maxscale",
                "parameters": [
                    {
                        "default_value": true,
                        "description": "Admin interface authentication.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_auth",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "description": "Admin interface is enabled.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_enabled",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "description": "Enable admin GUI.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_gui",
                        "type": "bool"
                    },
                    {
                        "default_value": "127.0.0.1",
                        "description": "Admin interface host.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_host",
                        "type": "string"
                    },
                    {
                        "default_value": true,
                        "description": "Log admin interface authentication failures.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "admin_log_auth_failures",
                        "type": "bool"
                    },
                    {
                        "description": "PAM service for read-only users.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_pam_readonly_service",
                        "type": "string"
                    },
                    {
                        "description": "PAM service for read-write users.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_pam_readwrite_service",
                        "type": "string"
                    },
                    {
                        "default_value": 8989,
                        "description": "Admin interface port.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_port",
                        "type": "int"
                    },
                    {
                        "default_value": true,
                        "description": "Only serve GUI over HTTPS.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_secure_gui",
                        "type": "bool"
                    },
                    {
                        "description": "Admin SSL CA cert",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_ssl_ca_cert",
                        "type": "string"
                    },
                    {
                        "description": "Admin SSL cert",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_ssl_cert",
                        "type": "string"
                    },
                    {
                        "description": "Admin SSL key",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_ssl_key",
                        "type": "string"
                    },
                    {
                        "default_value": "MAX",
                        "description": "Minimum required TLS protocol version for the REST API",
                        "enum_values": [
                            "MAX",
                            "TLSv10",
                            "TLSv11",
                            "TLSv12",
                            "TLSv13"
                        ],
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_ssl_version",
                        "type": "enum"
                    },
                    {
                        "default_value": 10000,
                        "description": "Connection timeout for fetching user accounts.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "auth_connect_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": 10000,
                        "description": "Read timeout for fetching user accounts (deprecated).",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "auth_read_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": 10000,
                        "description": "Write timeout for for fetching user accounts (deprecated).",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "auth_write_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "description": "Debug options",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "debug",
                        "type": "string"
                    },
                    {
                        "default_value": "never",
                        "description": "In what circumstances should the last statements that a client sent be dumped.",
                        "enum_values": [
                            "on_close",
                            "on_error",
                            "never"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "dump_last_statements",
                        "type": "enum"
                    },
                    {
                        "default_value": true,
                        "description": "Specifies whether persisted configuration files should be loaded on startup.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "load_persisted_configs",
                        "type": "bool"
                    },
                    {
                        "description": "Local address to use when connecting.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "local_address",
                        "type": "string"
                    },
                    {
                        "default_value": false,
                        "description": "Specifies whether debug messages should be logged (meaningful only with debug builds).",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_debug",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Specifies whether info messages should be logged.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_info",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "description": "Specifies whether notice messages should be logged.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_notice",
                        "type": "bool"
                    },
                    {
                        "default_value": {
                            "count": 0,
                            "suppress": 0,
                            "window": 0
                        },
                        "description": "Limit the amount of identical log messages than can be logged during a certain time period.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_throttling",
                        "type": "throttling"
                    },
                    {
                        "default_value": false,
                        "description": "Log a warning when a user with super privilege logs in.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "log_warn_super_user",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "description": "Specifies whether warning messages should be logged.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_warning",
                        "type": "bool"
                    },
                    {
                        "default_value": 10,
                        "description": "The maximum number of authentication failures that are tolerated before a host is temporarily blocked.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "max_auth_errors_until_block",
                        "type": "int"
                    },
                    {
                        "default_value": true,
                        "description": "Log to MaxScale's own log.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "maxlog",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Enable or disable high precision timestamps.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ms_timestamp",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "True if MaxScale is in passive mode.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "passive",
                        "type": "bool"
                    },
                    {
                        "default_value": "qc_sqlite",
                        "description": "The name of the query classifier to load.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "query_classifier",
                        "type": "string"
                    },
                    {
                        "description": "Arguments for the query classifier.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "query_classifier_args",
                        "type": "string"
                    },
                    {
                        "default_value": 0,
                        "description": "Maximum amount of memory used by query classifier cache.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "query_classifier_cache_size",
                        "type": "size"
                    },
                    {
                        "default_value": 1,
                        "description": "Number of times an interrupted query is retried.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "query_retries",
                        "type": "int"
                    },
                    {
                        "default_value": 5000,
                        "description": "The total timeout in seconds for any retried queries.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "query_retry_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": 0,
                        "description": "How often should the load of the worker threads be checked and rebalancing be made.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "rebalance_period",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": 20,
                        "description": "If the difference in load between the thread with the maximum load and the thread with the minimum load is larger than the value of this parameter, then work will be moved from the former to the latter.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "rebalance_threshold",
                        "type": "int"
                    },
                    {
                        "default_value": 10,
                        "description": "The load of how many seconds should be taken into account when rebalancing.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "rebalance_window",
                        "type": "count"
                    },
                    {
                        "default_value": 0,
                        "description": "How many statements should be retained for each session for debugging purposes.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "retain_last_statements",
                        "type": "count"
                    },
                    {
                        "default_value": 0,
                        "description": "How many log entries are stored in the session specific trace log.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "session_trace",
                        "type": "count"
                    },
                    {
                        "default_value": false,
                        "description": "Skip service and monitor permission checks.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "skip_permission_checks",
                        "type": "bool"
                    },
                    {
                        "default_value": "default",
                        "description": "The query classifier sql mode.",
                        "enum_values": [
                            "default",
                            "oracle"
                        ],
                        "mandatory": false,
                        "modifiable": false,
                        "name": "sql_mode",
                        "type": "enum"
                    },
                    {
                        "default_value": true,
                        "description": "Log to syslog.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "syslog",
                        "type": "bool"
                    },
                    {
                        "default_value": 1,
                        "description": "This parameter specifies how many threads will be used for handling the routing.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "threads",
                        "type": "count"
                    },
                    {
                        "default_value": 0,
                        "description": "How often the users will be refreshed.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "users_refresh_interval",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": 30000,
                        "description": "How often the users can be refreshed.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "users_refresh_time",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": 16777216,
                        "description": "High water mark of dcb write queue.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "writeq_high_water",
                        "type": "size"
                    },
                    {
                        "default_value": 8192,
                        "description": "Low water mark of dcb write queue.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "writeq_low_water",
                        "type": "size"
                    }
                ],
                "version": "2.5.14"
            },
            "id": "maxscale",
            "links": {
                "self": "http://localhost:8989/v1/modules/maxscale"
            },
            "type": "modules"
        },
        {
            "attributes": {
                "commands": [],
                "description": "servers",
                "maturity": "GA",
                "module_type": "servers",
                "parameters": [
                    {
                        "description": "Server address",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "address",
                        "type": "string"
                    },
                    {
                        "description": "Server authenticator (deprecated)",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "authenticator",
                        "type": "string"
                    },
                    {
                        "description": "Server disk space threshold",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "disk_space_threshold",
                        "type": "disk_space_limits"
                    },
                    {
                        "default_value": 0,
                        "description": "Server extra port",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "extra_port",
                        "type": "count"
                    },
                    {
                        "description": "Monitor password",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "monitorpw",
                        "type": "string"
                    },
                    {
                        "description": "Monitor user",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "monitoruser",
                        "type": "string"
                    },
                    {
                        "default_value": 0,
                        "description": "Maximum time that a connection can be in the pool",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "persistmaxtime",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": 0,
                        "description": "Maximum size of the persistent connection pool",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "persistpoolmax",
                        "type": "count"
                    },
                    {
                        "default_value": 3306,
                        "description": "Server port",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "port",
                        "type": "count"
                    },
                    {
                        "default_value": 0,
                        "description": "Server priority",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "priority",
                        "type": "count"
                    },
                    {
                        "description": "Server protocol (deprecated)",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "protocol",
                        "type": "string"
                    },
                    {
                        "default_value": false,
                        "description": "Enable proxy protocol",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "proxy_protocol",
                        "type": "bool"
                    },
                    {
                        "default_value": "primary",
                        "description": "Server rank",
                        "enum_values": [
                            "primary",
                            "secondary"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "rank",
                        "type": "enum"
                    },
                    {
                        "description": "Server UNIX socket",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "socket",
                        "type": "string"
                    },
                    {
                        "default_value": false,
                        "description": "Enable TLS for server",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "ssl",
                        "type": "bool"
                    },
                    {
                        "description": "TLS certificate authority",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "ssl_ca_cert",
                        "type": "path"
                    },
                    {
                        "description": "TLS public certificate",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "ssl_cert",
                        "type": "path"
                    },
                    {
                        "default_value": 9,
                        "description": "TLS certificate verification depth",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "ssl_cert_verify_depth",
                        "type": "count"
                    },
                    {
                        "description": "TLS cipher list",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "ssl_cipher",
                        "type": "string"
                    },
                    {
                        "description": "TLS private key",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "ssl_key",
                        "type": "path"
                    },
                    {
                        "default_value": false,
                        "description": "Verify TLS peer certificate",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "ssl_verify_peer_certificate",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Verify TLS peer host",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "ssl_verify_peer_host",
                        "type": "bool"
                    },
                    {
                        "default_value": "MAX",
                        "description": "Minimum TLS protocol version",
                        "enum_values": [
                            "MAX",
                            "TLSv10",
                            "TLSv11",
                            "TLSv12",
                            "TLSv13"
                        ],
                        "mandatory": false,
                        "modifiable": false,
                        "name": "ssl_version",
                        "type": "enum"
                    },
                    {
                        "default_value": "server",
                        "description": "Object type",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "type",
                        "type": "string"
                    }
                ],
                "version": "2.5.14"
            },
            "id": "servers",
            "links": {
                "self": "http://localhost:8989/v1/modules/servers"
            },
            "type": "modules"
        },
        {
            "attributes": {
                "api": "authenticator",
                "commands": [],
                "description": "Standard MySQL/MariaDB authentication (mysql_native_password)",
                "maturity": "GA",
                "module_type": "Authenticator",
                "parameters": [],
                "version": "V2.1.0"
            },
            "id": "MariaDBAuth",
            "links": {
                "self": "http://localhost:8989/v1/modules/MariaDBAuth"
            },
            "type": "modules"
        },
        {
            "attributes": {
                "api": "monitor",
                "commands": [
                    {
                        "attributes": {
                            "arg_max": 1,
                            "arg_min": 1,
                            "description": "Fetch result of the last scheduled command.",
                            "method": "GET",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                }
                            ]
                        },
                        "id": "fetch-cmd-result",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/fetch-cmd-result"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 1,
                            "arg_min": 1,
                            "description": "Fetch result of the last scheduled command.",
                            "method": "GET",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                }
                            ]
                        },
                        "id": "fetch-cmd-results",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/fetch-cmd-results"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 1,
                            "arg_min": 1,
                            "description": "Release any held server locks for 1 minute.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                }
                            ]
                        },
                        "id": "release-locks",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/release-locks"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 2,
                            "arg_min": 1,
                            "description": "Delete slave connections, delete binary logs and set up replication (dangerous)",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Master server (optional)",
                                    "required": false,
                                    "type": "[SERVER]"
                                }
                            ]
                        },
                        "id": "reset-replication",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/reset-replication"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 2,
                            "arg_min": 2,
                            "description": "Rejoin server to a cluster",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Joining server",
                                    "required": true,
                                    "type": "SERVER"
                                }
                            ]
                        },
                        "id": "rejoin",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/rejoin"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 1,
                            "arg_min": 1,
                            "description": "Perform master failover",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                }
                            ]
                        },
                        "id": "failover",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/failover"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 3,
                            "arg_min": 1,
                            "description": "Schedule master switchover without waiting for completion",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "New master (optional)",
                                    "required": false,
                                    "type": "[SERVER]"
                                },
                                {
                                    "description": "Current master (optional)",
                                    "required": false,
                                    "type": "[SERVER]"
                                }
                            ]
                        },
                        "id": "async-switchover",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-switchover"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 3,
                            "arg_min": 1,
                            "description": "Perform master switchover",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "New master (optional)",
                                    "required": false,
                                    "type": "[SERVER]"
                                },
                                {
                                    "description": "Current master (optional)",
                                    "required": false,
                                    "type": "[SERVER]"
                                }
                            ]
                        },
                        "id": "switchover",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/switchover"
                        },
                        "type": "module_command"
                    }
                ],
                "description": "A MariaDB Master/Slave replication monitor",
                "maturity": "GA",
                "module_type": "Monitor",
                "parameters": [
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "detect_replication_lag",
                        "type": "bool"
                    },
                    {
                        "mandatory": false,
                        "name": "detect_stale_master",
                        "type": "bool"
                    },
                    {
                        "mandatory": false,
                        "name": "detect_stale_slave",
                        "type": "bool"
                    },
                    {
                        "mandatory": false,
                        "name": "detect_standalone_master",
                        "type": "bool"
                    },
                    {
                        "default_value": 5,
                        "mandatory": false,
                        "name": "failcount",
                        "type": "count"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "ignore_external_masters",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "auto_failover",
                        "type": "bool"
                    },
                    {
                        "default_value": "90s",
                        "mandatory": false,
                        "name": "failover_timeout",
                        "type": "duration",
                        "unit": "s"
                    },
                    {
                        "default_value": "90s",
                        "mandatory": false,
                        "name": "switchover_timeout",
                        "type": "duration",
                        "unit": "s"
                    },
                    {
                        "mandatory": false,
                        "name": "replication_user",
                        "type": "string"
                    },
                    {
                        "mandatory": false,
                        "name": "replication_password",
                        "type": "password string"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "replication_master_ssl",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "mandatory": false,
                        "name": "verify_master_failure",
                        "type": "bool"
                    },
                    {
                        "default_value": "10s",
                        "mandatory": false,
                        "name": "master_failure_timeout",
                        "type": "duration",
                        "unit": "s"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "auto_rejoin",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "enforce_read_only_slaves",
                        "type": "bool"
                    },
                    {
                        "mandatory": false,
                        "name": "servers_no_promotion",
                        "type": "serverlist"
                    },
                    {
                        "mandatory": false,
                        "name": "promotion_sql_file",
                        "type": "path"
                    },
                    {
                        "mandatory": false,
                        "name": "demotion_sql_file",
                        "type": "path"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "switchover_on_low_disk_space",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "mandatory": false,
                        "name": "maintenance_on_low_disk_space",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "mandatory": false,
                        "name": "handle_events",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "mandatory": false,
                        "name": "assume_unique_hostnames",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "enforce_simple_topology",
                        "type": "bool"
                    },
                    {
                        "default_value": "none",
                        "enum_values": [
                            "none",
                            "majority_of_running",
                            "majority_of_all"
                        ],
                        "mandatory": false,
                        "name": "cooperative_monitoring_locks",
                        "type": "enum"
                    },
                    {
                        "default_value": "primary_monitor_master",
                        "enum_values": [
                            "none",
                            "connecting_slave",
                            "connected_slave",
                            "running_slave",
                            "primary_monitor_master"
                        ],
                        "mandatory": false,
                        "name": "master_conditions",
                        "type": "enum_mask"
                    },
                    {
                        "default_value": "none",
                        "enum_values": [
                            "linked_master",
                            "running_master",
                            "writable_master",
                            "primary_monitor_master",
                            "none"
                        ],
                        "mandatory": false,
                        "name": "slave_conditions",
                        "type": "enum_mask"
                    },
                    {
                        "mandatory": true,
                        "name": "user",
                        "type": "string"
                    },
                    {
                        "mandatory": true,
                        "name": "password",
                        "type": "password string"
                    },
                    {
                        "default_value": "2000ms",
                        "mandatory": false,
                        "name": "monitor_interval",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "3s",
                        "mandatory": false,
                        "name": "backend_connect_timeout",
                        "type": "duration",
                        "unit": "s"
                    },
                    {
                        "default_value": "3s",
                        "mandatory": false,
                        "name": "backend_read_timeout",
                        "type": "duration",
                        "unit": "s"
                    },
                    {
                        "default_value": "3s",
                        "mandatory": false,
                        "name": "backend_write_timeout",
                        "type": "duration",
                        "unit": "s"
                    },
                    {
                        "default_value": 1,
                        "mandatory": false,
                        "name": "backend_connect_attempts",
                        "type": "count"
                    },
                    {
                        "default_value": "28800s",
                        "mandatory": false,
                        "name": "journal_max_age",
                        "type": "duration",
                        "unit": "s"
                    },
                    {
                        "mandatory": false,
                        "name": "disk_space_threshold",
                        "type": "string"
                    },
                    {
                        "default_value": "0ms",
                        "mandatory": false,
                        "name": "disk_space_check_interval",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "mandatory": false,
                        "name": "script",
                        "type": "string"
                    },
                    {
                        "default_value": "90s",
                        "mandatory": false,
                        "name": "script_timeout",
                        "type": "duration",
                        "unit": "s"
                    },
                    {
                        "default_value": "all",
                        "enum_values": [
                            "all",
                            "master_down",
                            "master_up",
                            "slave_down",
                            "slave_up",
                            "server_down",
                            "server_up",
                            "synced_down",
                            "synced_up",
                            "donor_down",
                            "donor_up",
                            "lost_master",
                            "lost_slave",
                            "lost_synced",
                            "lost_donor",
                            "new_master",
                            "new_slave",
                            "new_synced",
                            "new_donor"
                        ],
                        "mandatory": false,
                        "name": "events",
                        "type": "enum_mask"
                    }
                ],
                "version": "V1.5.0"
            },
            "id": "mariadbmon",
            "links": {
                "self": "http://localhost:8989/v1/modules/mariadbmon"
            },
            "type": "modules"
        },
        {
            "attributes": {
                "api": "router",
                "commands": [],
                "description": "A Read/Write splitting router for enhancement read scalability",
                "maturity": "GA",
                "module_type": "Router",
                "parameters": [
                    {
                        "default_value": "false",
                        "enum_values": [
                            "false",
                            "off",
                            "0",
                            "true",
                            "on",
                            "1",
                            "none",
                            "local",
                            "global",
                            "fast"
                        ],
                        "mandatory": false,
                        "name": "causal_reads",
                        "type": "enum"
                    },
                    {
                        "default_value": "10000ms",
                        "mandatory": false,
                        "name": "causal_reads_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "delayed_retry",
                        "type": "bool"
                    },
                    {
                        "default_value": "10000ms",
                        "mandatory": false,
                        "name": "delayed_retry_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "disable_sescmd_history",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "lazy_connect",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "master_accept_reads",
                        "type": "bool"
                    },
                    {
                        "default_value": "fail_instantly",
                        "enum_values": [
                            "fail_instantly",
                            "fail_on_write",
                            "error_on_write"
                        ],
                        "mandatory": false,
                        "name": "master_failure_mode",
                        "type": "enum"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "master_reconnection",
                        "type": "bool"
                    },
                    {
                        "default_value": 50,
                        "mandatory": false,
                        "name": "max_sescmd_history",
                        "type": "count"
                    },
                    {
                        "default_value": "255",
                        "mandatory": false,
                        "name": "max_slave_connections",
                        "type": "string"
                    },
                    {
                        "default_value": "0ms",
                        "mandatory": false,
                        "name": "max_slave_replication_lag",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "optimistic_trx",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "prune_sescmd_history",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "mandatory": false,
                        "name": "retry_failed_reads",
                        "type": "bool"
                    },
                    {
                        "default_value": 255,
                        "mandatory": false,
                        "name": "slave_connections",
                        "type": "count"
                    },
                    {
                        "default_value": "LEAST_CURRENT_OPERATIONS",
                        "enum_values": [
                            "LEAST_GLOBAL_CONNECTIONS",
                            "LEAST_ROUTER_CONNECTIONS",
                            "LEAST_BEHIND_MASTER",
                            "LEAST_CURRENT_OPERATIONS",
                            "ADAPTIVE_ROUTING"
                        ],
                        "mandatory": false,
                        "name": "slave_selection_criteria",
                        "type": "enum"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "strict_multi_stmt",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "strict_sp_calls",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "transaction_replay",
                        "type": "bool"
                    },
                    {
                        "default_value": 5,
                        "mandatory": false,
                        "name": "transaction_replay_attempts",
                        "type": "count"
                    },
                    {
                        "default_value": 1073741824,
                        "mandatory": false,
                        "name": "transaction_replay_max_size",
                        "type": "size"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "transaction_replay_retry_on_deadlock",
                        "type": "bool"
                    },
                    {
                        "default_value": "all",
                        "enum_values": [
                            "all",
                            "master"
                        ],
                        "mandatory": false,
                        "name": "use_sql_variables_in",
                        "type": "enum"
                    },
                    {
                        "mandatory": false,
                        "name": "router_options",
                        "type": "string"
                    },
                    {
                        "mandatory": true,
                        "name": "user",
                        "type": "string"
                    },
                    {
                        "mandatory": true,
                        "name": "password",
                        "type": "password string"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "enable_root_user",
                        "type": "bool"
                    },
                    {
                        "default_value": 0,
                        "mandatory": false,
                        "name": "max_connections",
                        "type": "count"
                    },
                    {
                        "default_value": "0",
                        "mandatory": false,
                        "name": "connection_timeout",
                        "type": "duration",
                        "unit": "s"
                    },
                    {
                        "default_value": "0",
                        "mandatory": false,
                        "name": "net_write_timeout",
                        "type": "duration",
                        "unit": "s"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "auth_all_servers",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "mandatory": false,
                        "name": "strip_db_esc",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "mandatory": false,
                        "name": "localhost_match_wildcard_host",
                        "type": "bool"
                    },
                    {
                        "mandatory": false,
                        "name": "version_string",
                        "type": "string"
                    },
                    {
                        "default_value": true,
                        "mandatory": false,
                        "name": "log_auth_warnings",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "session_track_trx_state",
                        "type": "bool"
                    },
                    {
                        "default_value": -1,
                        "mandatory": false,
                        "name": "retain_last_statements",
                        "type": "int"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "session_trace",
                        "type": "bool"
                    },
                    {
                        "default_value": "primary",
                        "enum_values": [
                            "primary",
                            "secondary"
                        ],
                        "mandatory": false,
                        "name": "rank",
                        "type": "enum"
                    },
                    {
                        "default_value": "300s",
                        "mandatory": false,
                        "name": "connection_keepalive",
                        "type": "duration",
                        "unit": "s"
                    }
                ],
                "version": "V1.1.0"
            },
            "id": "readwritesplit",
            "links": {
                "self": "http://localhost:8989/v1/modules/readwritesplit"
            },
            "type": "modules"
        },
        {
            "attributes": {
                "api": "router",
                "commands": [],
                "description": "A connection based router to load balance based on connections",
                "maturity": "GA",
                "module_type": "Router",
                "parameters": [
                    {
                        "mandatory": false,
                        "name": "router_options",
                        "type": "string"
                    },
                    {
                        "mandatory": true,
                        "name": "user",
                        "type": "string"
                    },
                    {
                        "mandatory": true,
                        "name": "password",
                        "type": "password string"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "enable_root_user",
                        "type": "bool"
                    },
                    {
                        "default_value": 0,
                        "mandatory": false,
                        "name": "max_connections",
                        "type": "count"
                    },
                    {
                        "default_value": "0",
                        "mandatory": false,
                        "name": "connection_timeout",
                        "type": "duration",
                        "unit": "s"
                    },
                    {
                        "default_value": "0",
                        "mandatory": false,
                        "name": "net_write_timeout",
                        "type": "duration",
                        "unit": "s"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "auth_all_servers",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "mandatory": false,
                        "name": "strip_db_esc",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "mandatory": false,
                        "name": "localhost_match_wildcard_host",
                        "type": "bool"
                    },
                    {
                        "mandatory": false,
                        "name": "version_string",
                        "type": "string"
                    },
                    {
                        "default_value": true,
                        "mandatory": false,
                        "name": "log_auth_warnings",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "session_track_trx_state",
                        "type": "bool"
                    },
                    {
                        "default_value": -1,
                        "mandatory": false,
                        "name": "retain_last_statements",
                        "type": "int"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "session_trace",
                        "type": "bool"
                    },
                    {
                        "default_value": "primary",
                        "enum_values": [
                            "primary",
                            "secondary"
                        ],
                        "mandatory": false,
                        "name": "rank",
                        "type": "enum"
                    },
                    {
                        "default_value": "300s",
                        "mandatory": false,
                        "name": "connection_keepalive",
                        "type": "duration",
                        "unit": "s"
                    }
                ],
                "version": "V2.0.0"
            },
            "id": "readconnroute",
            "links": {
                "self": "http://localhost:8989/v1/modules/readconnroute"
            },
            "type": "modules"
        },
        {
            "attributes": {
                "api": "filter",
                "commands": [],
                "description": "A hint parsing filter",
                "maturity": "Alpha",
                "module_type": "Filter",
                "parameters": [],
                "version": "V1.0.0"
            },
            "id": "hintfilter",
            "links": {
                "self": "http://localhost:8989/v1/modules/hintfilter"
            },
            "type": "modules"
        },
        {
            "attributes": {
                "api": "filter",
                "commands": [
                    {
                        "attributes": {
                            "arg_max": 3,
                            "arg_min": 1,
                            "description": "Show unified log file as a JSON array",
                            "method": "GET",
                            "parameters": [
                                {
                                    "description": "Filter to read logs from",
                                    "required": true,
                                    "type": "FILTER"
                                },
                                {
                                    "description": "Start reading from this line",
                                    "required": false,
                                    "type": "[STRING]"
                                },
                                {
                                    "description": "Stop reading at this line (exclusive)",
                                    "required": false,
                                    "type": "[STRING]"
                                }
                            ]
                        },
                        "id": "log",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/qlafilter/log"
                        },
                        "type": "module_command"
                    }
                ],
                "description": "A simple query logging filter",
                "maturity": "GA",
                "module_type": "Filter",
                "parameters": [
                    {
                        "mandatory": false,
                        "name": "match",
                        "type": "regular expression"
                    },
                    {
                        "mandatory": false,
                        "name": "exclude",
                        "type": "regular expression"
                    },
                    {
                        "mandatory": false,
                        "name": "user",
                        "type": "string"
                    },
                    {
                        "mandatory": false,
                        "name": "source",
                        "type": "string"
                    },
                    {
                        "mandatory": true,
                        "name": "filebase",
                        "type": "string"
                    },
                    {
                        "default_value": "ignorecase",
                        "enum_values": [
                            "ignorecase",
                            "case",
                            "extended"
                        ],
                        "mandatory": false,
                        "name": "options",
                        "type": "enum_mask"
                    },
                    {
                        "default_value": "session",
                        "enum_values": [
                            "session",
                            "unified",
                            "stdout"
                        ],
                        "mandatory": false,
                        "name": "log_type",
                        "type": "enum_mask"
                    },
                    {
                        "default_value": "date,user,query",
                        "enum_values": [
                            "service",
                            "session",
                            "date",
                            "user",
                            "query",
                            "reply_time",
                            "default_db"
                        ],
                        "mandatory": false,
                        "name": "log_data",
                        "type": "enum_mask"
                    },
                    {
                        "default_value": "\" \"",
                        "mandatory": false,
                        "name": "newline_replacement",
                        "type": "quoted string"
                    },
                    {
                        "default_value": ",",
                        "mandatory": false,
                        "name": "separator",
                        "type": "quoted string"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "flush",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "append",
                        "type": "bool"
                    }
                ],
                "version": "V1.1.1"
            },
            "id": "qlafilter",
            "links": {
                "self": "http://localhost:8989/v1/modules/qlafilter"
            },
            "type": "modules"
        },
        {
            "attributes": {
                "api": "protocol",
                "commands": [],
                "description": "The client to MaxScale MySQL protocol implementation",
                "maturity": "GA",
                "module_type": "Protocol",
                "parameters": [
                    {
                        "mandatory": true,
                        "name": "protocol",
                        "type": "string"
                    },
                    {
                        "mandatory": false,
                        "name": "port",
                        "type": "count"
                    },
                    {
                        "mandatory": false,
                        "name": "socket",
                        "type": "string"
                    },
                    {
                        "default_value": "",
                        "mandatory": false,
                        "name": "authenticator_options",
                        "type": "string"
                    },
                    {
                        "default_value": "::",
                        "mandatory": false,
                        "name": "address",
                        "type": "string"
                    },
                    {
                        "mandatory": false,
                        "name": "authenticator",
                        "type": "string"
                    },
                    {
                        "default_value": "false",
                        "enum_values": [
                            "required",
                            "true",
                            "yes",
                            "on",
                            "1",
                            "disabled",
                            "false",
                            "no",
                            "off",
                            "0"
                        ],
                        "mandatory": false,
                        "name": "ssl",
                        "type": "enum"
                    },
                    {
                        "mandatory": false,
                        "name": "ssl_cert",
                        "type": "path"
                    },
                    {
                        "mandatory": false,
                        "name": "ssl_key",
                        "type": "path"
                    },
                    {
                        "mandatory": false,
                        "name": "ssl_ca_cert",
                        "type": "path"
                    },
                    {
                        "mandatory": false,
                        "name": "ssl_crl",
                        "type": "path"
                    },
                    {
                        "default_value": "MAX",
                        "enum_values": [
                            "MAX",
                            "TLSv10",
                            "TLSv11",
                            "TLSv12",
                            "TLSv13"
                        ],
                        "mandatory": false,
                        "name": "ssl_version",
                        "type": "enum"
                    },
                    {
                        "default_value": 9,
                        "mandatory": false,
                        "name": "ssl_cert_verify_depth",
                        "type": "count"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "ssl_verify_peer_certificate",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "mandatory": false,
                        "name": "ssl_verify_peer_host",
                        "type": "bool"
                    },
                    {
                        "mandatory": false,
                        "name": "ssl_cipher",
                        "type": "string"
                    },
                    {
                        "mandatory": false,
                        "name": "sql_mode",
                        "type": "string"
                    },
                    {
                        "mandatory": false,
                        "name": "connection_init_sql_file",
                        "type": "path"
                    }
                ],
                "version": "V1.1.0"
            },
            "id": "MariaDBClient",
            "links": {
                "self": "http://localhost:8989/v1/modules/MariaDBClient"
            },
            "type": "modules"
        },
        {
            "attributes": {
                "api": "query_classifier",
                "commands": [],
                "description": "Query classifier using sqlite.",
                "maturity": "GA",
                "module_type": "QueryClassifier",
                "parameters": [],
                "version": "V1.0.0"
            },
            "id": "qc_sqlite",
            "links": {
                "self": "http://localhost:8989/v1/modules/qc_sqlite"
            },
            "type": "modules"
        }
    ],
    "links": {
        "self": "http://localhost:8989/v1/maxscale/modules/"
    }
}
```

## Call a module command

For read-only commands:

```
GET /v1/maxscale/modules/:module/:command
```

For commands that can modify data:

```
POST /v1/maxscale/modules/:module/:command
```

Modules can expose commands that can be called via the REST API. The module
resource lists all commands in the `data.attributes.commands` list. Each value
is a command sub-resource identified by its `id` field and the HTTP method the
command uses is defined by the `attributes.method` field.

The _:module_ in the URI must be a valid name of a loaded module and _:command_
must be a valid command identifier that is exposed by that module. All
parameters to the module commands are passed as HTTP request parameters.

Here is an example POST requests to the dbfwfilter module command _reload_ with
two parameters, the name of the filter instance and the path to a file:

```
POST /v1/maxscale/modules/dbfwfilter/reload?my-dbfwfilter-instance&/path/to/file.txt
```

#### Response

Command with output:

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/modules/dbfwfilter/rules/json"
    },
    "meta": [ // Output of module command (module dependent)
        {
            "name": "test3",
            "type": "COLUMN",
            "times_matched": 0
        }
    ]
}
```

The contents of the `meta` field will contain the output of the module
command. This output depends on the command that is being executed. It can
contain any valid JSON value.

Command with no output:

`Status: 204 No Content`

## Classify a statement

```
GET /v1/maxscale/query_classifier/classify?sql=<statement>
```

Classify provided statement and return the result.

#### Response

`Status: 200 OK`

```
GET /v1/maxscale/query_classifier/classify?sql=SELECT+1
```

```javascript
{
    "data": {
        "attributes": {
            "fields": [],
            "functions": [],
            "has_where_clause": false,
            "operation": "QUERY_OP_SELECT",
            "parse_result": "QC_QUERY_PARSED",
            "type_mask": "QUERY_TYPE_READ"
        },
        "id": "classify",
        "type": "classify"
    },
    "links": {
        "self": "http://localhost:8989/v1/maxscale/query_classifier/classify"
    }
}
```
