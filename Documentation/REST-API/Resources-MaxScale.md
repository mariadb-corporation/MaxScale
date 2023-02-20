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
            "activated_at": "Fri, 27 Jan 2023 13:21:28 GMT",
            "commit": "1cd89e5840c7f548f1c192f15ff15cf81d3a873c",
            "config_sync": null,
            "parameters": {
                "admin_auth": true,
                "admin_enabled": true,
                "admin_gui": true,
                "admin_host": "127.0.0.1",
                "admin_jwt_algorithm": "auto",
                "admin_jwt_key": null,
                "admin_jwt_max_age": "86400000ms",
                "admin_log_auth_failures": true,
                "admin_oidc_url": null,
                "admin_pam_readonly_service": null,
                "admin_pam_readwrite_service": null,
                "admin_port": 8989,
                "admin_secure_gui": false,
                "admin_ssl_ca": null,
                "admin_ssl_cert": null,
                "admin_ssl_key": null,
                "admin_ssl_version": "MAX",
                "admin_verify_url": null,
                "auth_connect_timeout": "10000ms",
                "auth_read_timeout": "10000ms",
                "auth_write_timeout": "10000ms",
                "auto_tune": [],
                "cachedir": "/var/cache/maxscale",
                "config_sync_cluster": null,
                "config_sync_interval": "5000ms",
                "config_sync_password": "*****",
                "config_sync_timeout": "10000ms",
                "config_sync_user": null,
                "connector_plugindir": "/usr/lib64/maxscale/plugin",
                "datadir": "/var/lib/maxscale",
                "debug": null,
                "dump_last_statements": "never",
                "execdir": "/usr/bin",
                "key_manager": "none",
                "language": "/var/lib/maxscale",
                "libdir": "/usr/lib64/maxscale",
                "load_persisted_configs": true,
                "local_address": null,
                "log_debug": false,
                "log_info": false,
                "log_notice": true,
                "log_throttling": {
                    "count": 10,
                    "suppress": 10000,
                    "window": 1000
                },
                "log_warn_super_user": false,
                "log_warning": true,
                "logdir": "/var/log/maxscale",
                "max_auth_errors_until_block": 10,
                "max_read_amount": 0,
                "maxlog": true,
                "module_configdir": "/etc/maxscale.modules.d",
                "ms_timestamp": false,
                "passive": false,
                "persist_runtime_changes": true,
                "persistdir": "/var/lib/maxscale/maxscale.cnf.d",
                "piddir": "/var/run/maxscale",
                "query_classifier": "qc_sqlite",
                "query_classifier_args": null,
                "query_classifier_cache_size": 5004527616,
                "query_retries": 1,
                "query_retry_timeout": "5000ms",
                "rebalance_period": "0ms",
                "rebalance_threshold": 20,
                "rebalance_window": 10,
                "retain_last_statements": 0,
                "session_trace": 0,
                "skip_name_resolve": false,
                "skip_permission_checks": false,
                "sql_mode": "default",
                "syslog": false,
                "threads": 3,
                "threads_max": 256,
                "users_refresh_interval": "0ms",
                "users_refresh_time": "0ms",
                "writeq_high_water": 65536,
                "writeq_low_water": 1024
            },
            "process_datadir": "/var/lib/maxscale/data1",
            "started_at": "Fri, 27 Jan 2023 13:21:28 GMT",
            "system": {
                "machine": {
                    "cores_available": 8,
                    "cores_physical": 8,
                    "cores_virtual": 8.0,
                    "memory_available": 33363517440,
                    "memory_physical": 33363517440
                },
                "maxscale": {
                    "query_classifier_cache_size": 5004527616,
                    "threads": 3
                },
                "os": {
                    "machine": "x86_64",
                    "nodename": "monolith",
                    "release": "6.1.6-100.fc36.x86_64",
                    "sysname": "Linux",
                    "version": "#1 SMP PREEMPT_DYNAMIC Sat Jan 14 17:00:40 UTC 2023"
                }
            },
            "uptime": 12,
            "version": "22.08.5"
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
`data.attributes.parameters` object. The parameters that can be modified are
listed in the `/v1/maxscale/modules/maxscale` endpoint and have the `modifiable`
value set to `true`.

#### Response

Parameters modified:

`Status: 204 No Content`

Invalid JSON body:

`Status: 400 Bad Request`

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
                "current_descriptors": 5,
                "errors": 0,
                "hangups": 0,
                "listening": true,
                "load": {
                    "last_hour": 0,
                    "last_minute": 0,
                    "last_second": 0
                },
                "max_event_queue_length": 1,
                "max_exec_time": 0,
                "max_queue_time": 0,
                "memory": {
                    "query_classifier": 0,
                    "sessions": 0,
                    "total": 0,
                    "zombies": 0
                },
                "query_classifier_cache": {
                    "evictions": 0,
                    "hits": 0,
                    "inserts": 0,
                    "misses": 0,
                    "size": 0
                },
                "reads": 20,
                "sessions": 0,
                "state": "Active",
                "total_descriptors": 5,
                "writes": 0,
                "zombies": 0
            }
        },
        "id": "0",
        "links": {
            "self": "http://localhost:8989/v1/threads/0/"
        },
        "type": "threads"
    },
    "links": {
        "self": "http://localhost:8989/v1/maxscale/threads/0/"
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
                    "current_descriptors": 5,
                    "errors": 0,
                    "hangups": 0,
                    "listening": true,
                    "load": {
                        "last_hour": 0,
                        "last_minute": 0,
                        "last_second": 0
                    },
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0,
                    "memory": {
                        "query_classifier": 0,
                        "sessions": 0,
                        "total": 0,
                        "zombies": 0
                    },
                    "query_classifier_cache": {
                        "evictions": 0,
                        "hits": 0,
                        "inserts": 0,
                        "misses": 0,
                        "size": 0
                    },
                    "reads": 21,
                    "sessions": 0,
                    "state": "Active",
                    "total_descriptors": 5,
                    "writes": 0,
                    "zombies": 0
                }
            },
            "id": "0",
            "links": {
                "self": "http://localhost:8989/v1/threads/0/"
            },
            "type": "threads"
        },
        {
            "attributes": {
                "stats": {
                    "accepts": 1,
                    "avg_event_queue_length": 1,
                    "current_descriptors": 8,
                    "errors": 0,
                    "hangups": 0,
                    "listening": true,
                    "load": {
                        "last_hour": 0,
                        "last_minute": 0,
                        "last_second": 0
                    },
                    "max_event_queue_length": 2,
                    "max_exec_time": 0,
                    "max_queue_time": 0,
                    "memory": {
                        "query_classifier": 1618,
                        "sessions": 70191,
                        "total": 71809,
                        "zombies": 0
                    },
                    "query_classifier_cache": {
                        "evictions": 0,
                        "hits": 0,
                        "inserts": 3,
                        "misses": 4,
                        "size": 1618
                    },
                    "reads": 35,
                    "sessions": 1,
                    "state": "Active",
                    "total_descriptors": 8,
                    "writes": 17,
                    "zombies": 0
                }
            },
            "id": "1",
            "links": {
                "self": "http://localhost:8989/v1/threads/1/"
            },
            "type": "threads"
        },
        {
            "attributes": {
                "stats": {
                    "accepts": 0,
                    "avg_event_queue_length": 1,
                    "current_descriptors": 5,
                    "errors": 0,
                    "hangups": 0,
                    "listening": true,
                    "load": {
                        "last_hour": 0,
                        "last_minute": 0,
                        "last_second": 0
                    },
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0,
                    "memory": {
                        "query_classifier": 0,
                        "sessions": 0,
                        "total": 0,
                        "zombies": 0
                    },
                    "query_classifier_cache": {
                        "evictions": 0,
                        "hits": 0,
                        "inserts": 0,
                        "misses": 0,
                        "size": 0
                    },
                    "reads": 20,
                    "sessions": 0,
                    "state": "Active",
                    "total_descriptors": 5,
                    "writes": 0,
                    "zombies": 0
                }
            },
            "id": "2",
            "links": {
                "self": "http://localhost:8989/v1/threads/2/"
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

**Note:** The parameters in this endpoint are a subset of the parameters in the
  `/v1/maxscale` endpoint. Because of this, the parameters in this endpoint are
  deprecated as of MaxScale 6.0.

**Note:** In MaxScale 2.5 the `log_throttling` and `ms_timestamp` parameters
  were incorrectly named as `throttling` and `highprecision`. In MaxScale 6,
  the parameter names are now correct which means the parameters declared here
  aren't fully backwards compatible.

#### Response

`Status: 200 OK`

```javascript
{
    "data": {
        "attributes": {
            "log_file": "/var/log/maxscale/maxscale.log",
            "log_priorities": [
                "alert",
                "error",
                "warning",
                "notice"
            ],
            "parameters": {
                "log_debug": false,
                "log_info": false,
                "log_notice": true,
                "log_throttling": {
                    "count": 10,
                    "suppress": 10000,
                    "window": 1000
                },
                "log_warning": true,
                "maxlog": true,
                "ms_timestamp": false,
                "syslog": false
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

## Get log data

```
GET /v1/maxscale/logs/data
```

Get the contents of the MaxScale logs. This endpoint was added in MaxScale 6.

To navigate the log, use the `prev` link to move backwards to older log
entries. The latest log entries can be read with the `last` link.

The entries are sorted in ascending order by the time they were logged. This
means that with the default parameters, the latest logged event is the last
element in the returned array.

#### Parameters

This endpoint supports the following parameters:

- `page[size]`

  - Set number of rows of data to read. By default, 50 rows of data are read
    from the log.

- `page[cursor]`

  - Set position from where the log data is retrieved. The default position to
    retrieve the log data is the end of the log.

    This value should not be modified by the user and the values returned in the
    `links` object should be used instead. This way the navigation will provide
    a consistent view of the log that does not overlap.

    Optionally, the `id` values in the returned data can be used as the values
    for this parameter to read data from a known point in the file.

- `priority`

  - Include messages only from these log levels. The default is to include all
    messages.

    The value given should be a comma-separated list of log priorities. The
    priorities are `alert`, `error`, `warning`, `notice`, `info` and
    `debug`. Note that the `debug` log level is only used in debug builds of
    MaxScale.

#### Response

`Status: 200 OK`

```javascript
{
    "data": {
        "attributes": {
            "log": [
                {
                    "id": "42",
                    "message": "Server 'server2' charset: utf8mb4",
                    "priority": "notice",
                    "timestamp": "2023-01-27 13:21:32"
                },
                {
                    "id": "43",
                    "message": "Server changed state: server2[127.0.0.1:3001]: slave_up. [Auth Error, Down] -> [Slave, Running]",
                    "priority": "notice",
                    "timestamp": "2023-01-27 13:21:32"
                },
                {
                    "id": "44",
                    "message": "The function '=' is not found in the canonical statement 'INSERT INTO test.t1(id) VALUES (?), (?), (?)' created from the statement 'INSERT INTO test.t1(id) VALUES (1), (2), (3)'.",
                    "module": "qc_sqlite",
                    "priority": "warning",
                    "session": "1",
                    "timestamp": "2023-01-27 13:21:40"
                }
            ],
            "log_source": "maxlog"
        },
        "id": "log_data",
        "type": "log_data"
    },
    "links": {
        "last": "http://localhost:8989/v1/maxscale/logs/data/?page%5Bsize%5D=3",
        "prev": "http://localhost:8989/v1/maxscale/logs/data/?page%5Bcursor%5D=39&page%5Bsize%5D=3",
        "self": "http://localhost:8989/v1/maxscale/logs/data/?page%5Bcursor%5D=45&page%5Bsize%5D=3"
    }
}
```

## Stream log data

```
GET /v1/maxscale/logs/stream
```

Stream the contents of the MaxScale logs. This endpoint was added in MaxScale 6.

This endpoint opens a [WebSocket](https://tools.ietf.org/html/rfc6455)
connection and streams the contents of the log to it. Each WebSocket message
will contain the JSON representation of the log message. The JSON is formatted
in the same way as the values in the `log` array of the `/v1/maxscale/logs/data`
endpoint:

```javascript
{
    "id": "572",
    "message": "MaxScale started with 8 worker threads, each with a stack size of 8388608 bytes.",
    "priority": "notice",
    "timestamp": "2020-09-25 10:01:29"
}
```

### Limitations

* If the client writes any data to the open socket, it will be treated as
  an error and the stream is closed.

* The WebSocket ping and close commands are not yet supported and will be
  treated as errors.

* When `maxlog` is used as source of log data, any log messages logged after log
  rotation will not be sent if the file was moved or truncated. To fetch new
  events after log rotation, reopen the WebSocket connection.

#### Parameters

This endpoint supports the following parameters:

- `page[cursor]`

  - Set position from where the log data is retrieved. The default position to
    retrieve the log data is the end of the log.

    To stream data from a known point, first read the data via the
    `/v1/maxscale/logs/data` endpoint and then use the `id` value of the newest
    log message (i.e. the first value in the `log` array) to start the stream.

- `priority`

  - Include messages only from these log levels. The default is to include all
    messages.

    The value given should be a comma-separated list of log priorities. The
    priorities are `alert`, `error`, `warning`, `notice`, `info` and
    `debug`. Note that the `debug` log level is only used in debug builds of
    MaxScale.

#### Response

Upgrade started:

`Status: 101 Switching Protocols`

Client didn't request a WebSocket upgrade:

`Status: 426 Upgrade Required`

## Update logging parameters

**Note:** The modification of logging parameters via this endpoint has
  deprecated in MaxScale 6.0. The parameters should be modified with the
  `/v1/maxscale` endpoint instead.

  Any PATCH requests done to this endpoint will be redirected to the
  `/v1/maxscale` endpoint. Due to the misspelling of the `ms_timestamp` and
  `log_throttling` parameters, this is not fully backwards compatible.

```
PATCH /v1/maxscale/logs
```

Update logging parameters. The request body must define updated values for the
`data.attributes.parameters` object. All logging parameters can be altered at runtime.

#### Response

Parameters modified:

`Status: 204 No Content`

Invalid JSON body:

`Status: 400 Bad Request`

## Flush and rotate log files

```
POST /v1/maxscale/logs/flush
```

Flushes any pending messages to disk and reopens the log files. The body of the
message is ignored.

#### Response

`Status: 204 No Content`

## Reload TLS certificates

```
POST /v1/maxscale/tls/reload
```

Reloads all TLS certificates for listeners and servers as well as the REST API
itself. If the reloading fails, the old certificates will remain in use for the
objects that failed to reload. This also causes the JWT signature keys to be
reloaded if one of the asymmetric key algorithms is being used. If JWTs are
being signed with a random symmetric keys, a new random key is created.

The reloading is not transactional: if a single listener or server fails to
reload its certificates, the remaining ones are not reloaded. This means that a
failed reload can partially reload certificates. The REST API certificates are
only reloaded if all other certificate reloads were successful.

#### Response

`Status: 204 No Content`

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

Any parameter with the `modifiable` value set to `true` can be modified
at runtime using a PATCH command on the corresponding object endpoint.

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
                    "description": "Causal reads mode",
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
                        "fast_global",
                        "fast",
                        "universal"
                    ],
                    "mandatory": false,
                    "modifiable": true,
                    "name": "causal_reads",
                    "type": "enum"
                },
                {
                    "default_value": "10000ms",
                    "description": "Timeout for the slave synchronization",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "causal_reads_timeout",
                    "type": "duration",
                    "unit": "ms"
                },
                {
                    "default_value": false,
                    "description": "Retry failed writes outside of transactions",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "delayed_retry",
                    "type": "bool"
                },
                {
                    "default_value": "10000ms",
                    "description": "Timeout for delayed_retry",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "delayed_retry_timeout",
                    "type": "duration",
                    "unit": "ms"
                },
                {
                    "default_value": false,
                    "description": "Create connections only when needed",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "lazy_connect",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "description": "Use master for reads",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "master_accept_reads",
                    "type": "bool"
                },
                {
                    "default_value": "fail_instantly",
                    "description": "Master failure mode behavior",
                    "enum_values": [
                        "fail_instantly",
                        "fail_on_write",
                        "error_on_write"
                    ],
                    "mandatory": false,
                    "modifiable": true,
                    "name": "master_failure_mode",
                    "type": "enum"
                },
                {
                    "default_value": false,
                    "description": "Reconnect to master",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "master_reconnection",
                    "type": "bool"
                },
                {
                    "default_value": 255,
                    "description": "Maximum number of slave connections",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "max_slave_connections",
                    "type": "count"
                },
                {
                    "default_value": "0ms",
                    "description": "Maximum allowed slave replication lag",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "max_slave_replication_lag",
                    "type": "duration",
                    "unit": "ms"
                },
                {
                    "default_value": false,
                    "description": "Optimistically offload transactions to slaves",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "optimistic_trx",
                    "type": "bool"
                },
                {
                    "default_value": true,
                    "description": "Automatically retry failed reads outside of transactions",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "retry_failed_reads",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "description": "Reuse identical prepared statements inside the same connection",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "reuse_prepared_statements",
                    "type": "bool"
                },
                {
                    "default_value": 255,
                    "description": "Starting number of slave connections",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "slave_connections",
                    "type": "count"
                },
                {
                    "default_value": "LEAST_CURRENT_OPERATIONS",
                    "description": "Slave selection criteria",
                    "enum_values": [
                        "LEAST_GLOBAL_CONNECTIONS",
                        "LEAST_ROUTER_CONNECTIONS",
                        "LEAST_BEHIND_MASTER",
                        "LEAST_CURRENT_OPERATIONS",
                        "ADAPTIVE_ROUTING"
                    ],
                    "mandatory": false,
                    "modifiable": true,
                    "name": "slave_selection_criteria",
                    "type": "enum"
                },
                {
                    "default_value": false,
                    "description": "Lock connection to master after multi-statement query",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "strict_multi_stmt",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "description": "Lock connection to master after a stored procedure is executed",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "strict_sp_calls",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "description": "Retry failed transactions",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "transaction_replay",
                    "type": "bool"
                },
                {
                    "default_value": 5,
                    "description": "Maximum number of times to retry a transaction",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "transaction_replay_attempts",
                    "type": "count"
                },
                {
                    "default_value": "full",
                    "description": "Type of checksum to calculate for results",
                    "enum_values": [
                        "full",
                        "result_only",
                        "no_insert_id"
                    ],
                    "mandatory": false,
                    "modifiable": true,
                    "name": "transaction_replay_checksum",
                    "type": "enum"
                },
                {
                    "default_value": 1073741824,
                    "description": "Maximum size of transaction to retry",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "transaction_replay_max_size",
                    "type": "size"
                },
                {
                    "default_value": false,
                    "description": "Retry transaction on deadlock",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "transaction_replay_retry_on_deadlock",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "description": "Retry transaction on checksum mismatch",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "transaction_replay_retry_on_mismatch",
                    "type": "bool"
                },
                {
                    "default_value": "0ms",
                    "description": "Timeout for transaction replay",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "transaction_replay_timeout",
                    "type": "duration",
                    "unit": "ms"
                },
                {
                    "default_value": "all",
                    "description": "Whether to route SQL variable modifications to all servers or only to the master",
                    "enum_values": [
                        "all",
                        "master"
                    ],
                    "mandatory": false,
                    "modifiable": true,
                    "name": "use_sql_variables_in",
                    "type": "enum"
                },
                {
                    "default_value": false,
                    "description": "Retrieve users from all backend servers instead of only one",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "auth_all_servers",
                    "type": "bool"
                },
                {
                    "default_value": "300000ms",
                    "description": "How often idle connections are pinged",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "connection_keepalive",
                    "type": "duration",
                    "unit": "ms"
                },
                {
                    "default_value": "0ms",
                    "description": "Connection idle timeout",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "connection_timeout",
                    "type": "duration",
                    "unit": "ms"
                },
                {
                    "default_value": false,
                    "description": "Disable session command history",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "disable_sescmd_history",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "description": "Allow the root user to connect to this service",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "enable_root_user",
                    "type": "bool"
                },
                {
                    "default_value": "-1ms",
                    "description": "Put connections into pool after session has been idle for this long",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "idle_session_pool_time",
                    "type": "duration",
                    "unit": "ms"
                },
                {
                    "default_value": true,
                    "description": "Match localhost to wildcard host",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "localhost_match_wildcard_host",
                    "type": "bool"
                },
                {
                    "default_value": true,
                    "description": "Log a warning when client authentication fails",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "log_auth_warnings",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "description": "Log debug messages for this service (debug builds only)",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "log_debug",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "description": "Log info messages for this service",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "log_info",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "description": "Log notice messages for this service",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "log_notice",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "description": "Log warning messages for this service",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "log_warning",
                    "type": "bool"
                },
                {
                    "default_value": 0,
                    "description": "Maximum number of connections",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "max_connections",
                    "type": "count"
                },
                {
                    "default_value": 50,
                    "description": "Session command history size",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "max_sescmd_history",
                    "type": "count"
                },
                {
                    "default_value": "60000ms",
                    "description": "How long a session can wait for a connection to become available",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "multiplex_timeout",
                    "type": "duration",
                    "unit": "ms"
                },
                {
                    "default_value": "0ms",
                    "description": "Network write timeout",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "net_write_timeout",
                    "type": "duration",
                    "unit": "ms"
                },
                {
                    "description": "Password for the user used to retrieve database users",
                    "mandatory": true,
                    "modifiable": true,
                    "name": "password",
                    "type": "password"
                },
                {
                    "default_value": true,
                    "description": "Prune old session command history if the limit is exceeded",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "prune_sescmd_history",
                    "type": "bool"
                },
                {
                    "default_value": "primary",
                    "description": "Service rank",
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
                    "default_value": -1,
                    "description": "Number of statements kept in memory",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "retain_last_statements",
                    "type": "int"
                },
                {
                    "default_value": false,
                    "description": "Enable session tracing for this service",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "session_trace",
                    "type": "bool"
                },
                {
                    "default_value": false,
                    "description": "Track session state using server responses",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "session_track_trx_state",
                    "type": "bool"
                },
                {
                    "default_value": true,
                    "description": "Strip escape characters from database names",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "strip_db_esc",
                    "type": "bool"
                },
                {
                    "description": "Username used to retrieve database users",
                    "mandatory": true,
                    "modifiable": true,
                    "name": "user",
                    "type": "string"
                },
                {
                    "description": "Load additional users from a file",
                    "mandatory": false,
                    "modifiable": false,
                    "name": "user_accounts_file",
                    "type": "path"
                },
                {
                    "default_value": "add_when_load_ok",
                    "description": "When and how the user accounts file is used",
                    "enum_values": [
                        "add_when_load_ok",
                        "file_only_always"
                    ],
                    "mandatory": false,
                    "modifiable": false,
                    "name": "user_accounts_file_usage",
                    "type": "enum"
                },
                {
                    "description": "Custom version string to use",
                    "mandatory": false,
                    "modifiable": true,
                    "name": "version_string",
                    "type": "string"
                }
            ],
            "version": "V1.1.0"
        },
        "id": "readwritesplit",
        "links": {
            "self": "http://localhost:8989/v1/modules/readwritesplit/"
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
                        "default_value": "auto",
                        "description": "JWT signature algorithm",
                        "enum_values": [
                            "auto",
                            "HS256",
                            "HS384",
                            "HS512",
                            "RS256",
                            "RS384",
                            "RS512",
                            "ES256",
                            "ES384",
                            "ES512",
                            "PS256",
                            "PS384",
                            "PS512",
                            "ED25519",
                            "ED448"
                        ],
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_jwt_algorithm",
                        "type": "enum"
                    },
                    {
                        "description": "Encryption key ID for symmetric signature algorithms. If left empty, MaxScale will generate a random key that is used to sign the JWT.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_jwt_key",
                        "type": "string"
                    },
                    {
                        "default_value": "86400000ms",
                        "description": "Maximum age of the JWTs generated by MaxScale",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "admin_jwt_max_age",
                        "type": "duration",
                        "unit": "ms"
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
                        "description": "Extra public certificates used to validate externally signed JWTs",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "admin_oidc_url",
                        "type": "string"
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
                        "name": "admin_ssl_ca",
                        "type": "path"
                    },
                    {
                        "deprecated": true,
                        "description": "Alias for 'admin_ssl_ca'",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_ssl_ca_cert",
                        "type": "path"
                    },
                    {
                        "description": "Admin SSL cert",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_ssl_cert",
                        "type": "path"
                    },
                    {
                        "description": "Admin SSL key",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_ssl_key",
                        "type": "path"
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
                        "description": "URL for third-party verification of client tokens",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "admin_verify_url",
                        "type": "string"
                    },
                    {
                        "default_value": "10000ms",
                        "description": "Connection timeout for fetching user accounts.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "auth_connect_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "10000ms",
                        "description": "Read timeout for fetching user accounts (deprecated).",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "auth_read_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "10000ms",
                        "description": "Write timeout for fetching user accounts (deprecated).",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "auth_write_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": [],
                        "description": "Specifies whether a MaxScale parameter whose value depends on a specific global server variable, should automatically be updated to match the variable's current value.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "auto_tune",
                        "type": "stringlist"
                    },
                    {
                        "description": "Cluster used for configuration synchronization. If left empty (i.e. value is \"\"), synchronization is not done.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "config_sync_cluster",
                        "type": "string"
                    },
                    {
                        "default_value": "5000ms",
                        "description": "How often to synchronize the configuration.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "config_sync_interval",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "*****",
                        "description": "Password for the user used for configuration synchronization.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "config_sync_password",
                        "type": "password"
                    },
                    {
                        "default_value": "10000ms",
                        "description": "Timeout for the configuration synchronization operations.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "config_sync_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "description": "User account used for configuration synchronization.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "config_sync_user",
                        "type": "string"
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
                        "default_value": "none",
                        "description": "Key manager type",
                        "enum_values": [
                            "none",
                            "file",
                            "kmip",
                            "vault"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "key_manager",
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
                            "count": 10,
                            "suppress": 10000,
                            "window": 1000
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
                        "default_value": 0,
                        "description": "Maximum amount of data read before return to epoll_wait.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "max_read_amount",
                        "type": "size"
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
                        "default_value": true,
                        "description": "Persist configurations changes done at runtime.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "persist_runtime_changes",
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
                        "default_value": 5004527616,
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
                        "default_value": "5000ms",
                        "description": "The total timeout in seconds for any retried queries.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "query_retry_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "0ms",
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
                        "description": "Do not resolve client IP addresses to hostnames during authentication",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "skip_name_resolve",
                        "type": "bool"
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
                        "default_value": false,
                        "description": "Log to syslog.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "syslog",
                        "type": "bool"
                    },
                    {
                        "default_value": 8,
                        "description": "This parameter specifies how many threads will be used for handling the routing.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "threads",
                        "type": "count"
                    },
                    {
                        "default_value": 256,
                        "description": "This parameter specifies a hard maximum for the number of routing threads.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "threads_max",
                        "type": "count"
                    },
                    {
                        "default_value": "0ms",
                        "description": "How often the users will be refreshed.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "users_refresh_interval",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "30000ms",
                        "description": "How often the users can be refreshed.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "users_refresh_time",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": 65536,
                        "description": "High water mark of dcb write queue.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "writeq_high_water",
                        "type": "size"
                    },
                    {
                        "default_value": 1024,
                        "description": "Low water mark of dcb write queue.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "writeq_low_water",
                        "type": "size"
                    }
                ],
                "version": "22.08.5"
            },
            "id": "maxscale",
            "links": {
                "self": "http://localhost:8989/v1/modules/maxscale/"
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
                        "default_value": 0,
                        "description": "Maximum routing connections",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "max_routing_connections",
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
                        "default_value": "0ms",
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
                        "type": "int"
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
                        "modifiable": true,
                        "name": "ssl",
                        "type": "bool"
                    },
                    {
                        "description": "TLS certificate authority",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_ca",
                        "type": "path"
                    },
                    {
                        "deprecated": true,
                        "description": "Alias for 'ssl_ca'",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_ca_cert",
                        "type": "path"
                    },
                    {
                        "description": "TLS public certificate",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_cert",
                        "type": "path"
                    },
                    {
                        "default_value": 9,
                        "description": "TLS certificate verification depth",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_cert_verify_depth",
                        "type": "count"
                    },
                    {
                        "description": "TLS cipher list",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_cipher",
                        "type": "string"
                    },
                    {
                        "description": "TLS private key",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_key",
                        "type": "path"
                    },
                    {
                        "default_value": false,
                        "description": "Verify TLS peer certificate",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_verify_peer_certificate",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Verify TLS peer host",
                        "mandatory": false,
                        "modifiable": true,
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
                        "modifiable": true,
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
                "version": "22.08.5"
            },
            "id": "servers",
            "links": {
                "self": "http://localhost:8989/v1/modules/servers/"
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
                "self": "http://localhost:8989/v1/modules/hintfilter/"
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
                "parameters": null,
                "version": "V2.1.0"
            },
            "id": "MariaDBAuth",
            "links": {
                "self": "http://localhost:8989/v1/modules/MariaDBAuth/"
            },
            "type": "modules"
        },
        {
            "attributes": {
                "api": "monitor",
                "commands": [
                    {
                        "attributes": {
                            "arg_max": 3,
                            "arg_min": 3,
                            "description": "Restore a server from a backup. Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Target server",
                                    "required": true,
                                    "type": "SERVER"
                                },
                                {
                                    "description": "Backup name",
                                    "required": true,
                                    "type": "STRING"
                                }
                            ]
                        },
                        "id": "async-restore-from-backup",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-restore-from-backup/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 3,
                            "arg_min": 3,
                            "description": "Create a backup with Mariabackup. Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Source server",
                                    "required": true,
                                    "type": "SERVER"
                                },
                                {
                                    "description": "Backup name",
                                    "required": true,
                                    "type": "STRING"
                                }
                            ]
                        },
                        "id": "async-create-backup",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-create-backup/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 3,
                            "arg_min": 2,
                            "description": "Rebuild a server with Mariabackup. Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Target server",
                                    "required": true,
                                    "type": "SERVER"
                                },
                                {
                                    "description": "Source server (optional)",
                                    "required": false,
                                    "type": "[SERVER]"
                                }
                            ]
                        },
                        "id": "async-rebuild-server",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-rebuild-server/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 2,
                            "arg_min": 2,
                            "description": "Set ColumnStore cluster readwrite. Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Timeout",
                                    "required": true,
                                    "type": "STRING"
                                }
                            ]
                        },
                        "id": "async-cs-set-readwrite",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-cs-set-readwrite/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 2,
                            "arg_min": 2,
                            "description": "Set ColumnStore cluster read-only. Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Timeout",
                                    "required": true,
                                    "type": "STRING"
                                }
                            ]
                        },
                        "id": "async-cs-set-readonly",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-cs-set-readonly/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 2,
                            "arg_min": 2,
                            "description": "Stop ColumnStore cluster. Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Timeout",
                                    "required": true,
                                    "type": "STRING"
                                }
                            ]
                        },
                        "id": "async-cs-stop-cluster",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-cs-stop-cluster/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 2,
                            "arg_min": 2,
                            "description": "Start ColumnStore cluster. Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Timeout",
                                    "required": true,
                                    "type": "STRING"
                                }
                            ]
                        },
                        "id": "async-cs-start-cluster",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-cs-start-cluster/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 1,
                            "arg_min": 1,
                            "description": "Get ColumnStore cluster status. Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                }
                            ]
                        },
                        "id": "async-cs-get-status",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-cs-get-status/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 1,
                            "arg_min": 1,
                            "description": "Get ColumnStore cluster status.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                }
                            ]
                        },
                        "id": "cs-get-status",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/cs-get-status/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 3,
                            "arg_min": 3,
                            "description": "Remove a node from a ColumnStore cluster. Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Hostname/IP of node to remove from ColumnStore cluster",
                                    "required": true,
                                    "type": "STRING"
                                },
                                {
                                    "description": "Timeout",
                                    "required": true,
                                    "type": "STRING"
                                }
                            ]
                        },
                        "id": "async-cs-remove-node",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-cs-remove-node/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 3,
                            "arg_min": 3,
                            "description": "Add a node to a ColumnStore cluster. Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Hostname/IP of node to add to ColumnStore cluster",
                                    "required": true,
                                    "type": "STRING"
                                },
                                {
                                    "description": "Timeout",
                                    "required": true,
                                    "type": "STRING"
                                }
                            ]
                        },
                        "id": "async-cs-add-node",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-cs-add-node/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 1,
                            "arg_min": 1,
                            "description": "Cancel the last scheduled command.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                }
                            ]
                        },
                        "id": "cancel-cmd",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/cancel-cmd/"
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
                        "id": "fetch-cmd-result",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/fetch-cmd-result/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 1,
                            "arg_min": 1,
                            "description": "Release any held server locks for 1 minute. Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                }
                            ]
                        },
                        "id": "async-release-locks",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-release-locks/"
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
                            "self": "http://localhost:8989/v1/modules/mariadbmon/release-locks/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 2,
                            "arg_min": 1,
                            "description": "Delete replica connections, delete binary logs and set up replication (dangerous). Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Primary server (optional)",
                                    "required": false,
                                    "type": "[SERVER]"
                                }
                            ]
                        },
                        "id": "async-reset-replication",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-reset-replication/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 2,
                            "arg_min": 1,
                            "description": "Delete replica connections, delete binary logs and set up replication (dangerous)",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "Primary server (optional)",
                                    "required": false,
                                    "type": "[SERVER]"
                                }
                            ]
                        },
                        "id": "reset-replication",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/reset-replication/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 2,
                            "arg_min": 2,
                            "description": "Rejoin server to a cluster. Does not wait for completion.",
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
                        "id": "async-rejoin",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-rejoin/"
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
                            "self": "http://localhost:8989/v1/modules/mariadbmon/rejoin/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 1,
                            "arg_min": 1,
                            "description": "Schedule primary failover. Does not wait for completion.",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                }
                            ]
                        },
                        "id": "async-failover",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-failover/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 1,
                            "arg_min": 1,
                            "description": "Perform primary failover",
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
                            "self": "http://localhost:8989/v1/modules/mariadbmon/failover/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 3,
                            "arg_min": 1,
                            "description": "Schedule primary switchover. Does not wait for completion",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "New primary (optional)",
                                    "required": false,
                                    "type": "[SERVER]"
                                },
                                {
                                    "description": "Current primary (optional)",
                                    "required": false,
                                    "type": "[SERVER]"
                                }
                            ]
                        },
                        "id": "async-switchover",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/async-switchover/"
                        },
                        "type": "module_command"
                    },
                    {
                        "attributes": {
                            "arg_max": 3,
                            "arg_min": 1,
                            "description": "Perform primary switchover",
                            "method": "POST",
                            "parameters": [
                                {
                                    "description": "Monitor name",
                                    "required": true,
                                    "type": "MONITOR"
                                },
                                {
                                    "description": "New primary (optional)",
                                    "required": false,
                                    "type": "[SERVER]"
                                },
                                {
                                    "description": "Current primary (optional)",
                                    "required": false,
                                    "type": "[SERVER]"
                                }
                            ]
                        },
                        "id": "switchover",
                        "links": {
                            "self": "http://localhost:8989/v1/modules/mariadbmon/switchover/"
                        },
                        "type": "module_command"
                    }
                ],
                "description": "A MariaDB Primary/Replica replication monitor",
                "maturity": "GA",
                "module_type": "Monitor",
                "parameters": [
                    {
                        "default_value": true,
                        "description": "Assume that hostnames are unique",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "assume_unique_hostnames",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Enable automatic server failover",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "auto_failover",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Enable automatic server rejoin",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "auto_rejoin",
                        "type": "bool"
                    },
                    {
                        "description": "Address of backup storage.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "backup_storage_address",
                        "type": "string"
                    },
                    {
                        "description": "Backup storage directory path.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "backup_storage_path",
                        "type": "string"
                    },
                    {
                        "default_value": "none",
                        "description": "Cooperative monitoring type",
                        "enum_values": [
                            "none",
                            "majority_of_running",
                            "majority_of_all"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "cooperative_monitoring_locks",
                        "type": "enum"
                    },
                    {
                        "description": "The API key used in communication with the ColumnStore admin daemon.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "cs_admin_api_key",
                        "type": "string"
                    },
                    {
                        "default_value": "/cmapi/0.4.0",
                        "description": "The base path to be used when accessing the ColumnStore administrative daemon. If, for instance, a daemon URL is https://localhost:8640/cmapi/0.4.0/node/start then the admin_base_path is \"/cmapi/0.4.0\".",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "cs_admin_base_path",
                        "type": "string"
                    },
                    {
                        "default_value": 8640,
                        "description": "Port of the ColumnStore administrative daemon.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "cs_admin_port",
                        "type": "count"
                    },
                    {
                        "description": "Path to SQL file that is executed during node demotion",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "demotion_sql_file",
                        "type": "path"
                    },
                    {
                        "default_value": false,
                        "description": "Enable read_only on all slave servers",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "enforce_read_only_slaves",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Enforce a simple topology",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "enforce_simple_topology",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Disable read_only on the current master server",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "enforce_writable_master",
                        "type": "bool"
                    },
                    {
                        "default_value": 5,
                        "description": "Number of failures to tolerate before failover occurs",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "failcount",
                        "type": "count"
                    },
                    {
                        "default_value": "90000ms",
                        "description": "Timeout for failover",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "failover_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": true,
                        "description": "Manage server-side events",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "handle_events",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "description": "Put the server into maintenance mode when it runs out of disk space",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "maintenance_on_low_disk_space",
                        "type": "bool"
                    },
                    {
                        "default_value": "primary_monitor_master",
                        "description": "Conditions that the master servers must meet",
                        "enum_values": [
                            "none",
                            "connecting_slave",
                            "connected_slave",
                            "running_slave",
                            "primary_monitor_master"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "master_conditions",
                        "type": "enum_mask"
                    },
                    {
                        "default_value": "10000ms",
                        "description": "Master failure timeout",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "master_failure_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "description": "Path to SQL file that is executed during node promotion",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "promotion_sql_file",
                        "type": "path"
                    },
                    {
                        "default_value": 4444,
                        "description": "Listen port used for transferring server backup.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "rebuild_port",
                        "type": "count"
                    },
                    {
                        "default_value": false,
                        "description": "Enable SSL when configuring replication",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "replication_master_ssl",
                        "type": "bool"
                    },
                    {
                        "default_value": "*****",
                        "description": "Password for the user that is used for replication",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "replication_password",
                        "type": "password"
                    },
                    {
                        "description": "User used for replication",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "replication_user",
                        "type": "string"
                    },
                    {
                        "default_value": -1,
                        "description": "Replication lag limit at which the script is run",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "script_max_replication_lag",
                        "type": "int"
                    },
                    {
                        "description": "List of servers that are never promoted",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "servers_no_promotion",
                        "type": "serverlist"
                    },
                    {
                        "default_value": "",
                        "description": "Conditions that the slave servers must meet",
                        "enum_values": [
                            "linked_master",
                            "running_master",
                            "writable_master",
                            "primary_monitor_master",
                            "none"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "slave_conditions",
                        "type": "enum_mask"
                    },
                    {
                        "default_value": true,
                        "description": "Is SSH host key check enabled.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssh_check_host_key",
                        "type": "bool"
                    },
                    {
                        "description": "SSH keyfile. Used for running remote commands on servers.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "ssh_keyfile",
                        "type": "path"
                    },
                    {
                        "default_value": 22,
                        "description": "SSH port. Used for running remote commands on servers.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssh_port",
                        "type": "count"
                    },
                    {
                        "default_value": "10000ms",
                        "description": "SSH connection and command timeout",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssh_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "description": "SSH username. Used for running remote commands on servers.",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "ssh_user",
                        "type": "string"
                    },
                    {
                        "default_value": false,
                        "description": "Perform a switchover when a server runs out of disk space",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "switchover_on_low_disk_space",
                        "type": "bool"
                    },
                    {
                        "default_value": "90000ms",
                        "description": "Timeout for switchover",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "switchover_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": true,
                        "description": "Verify master failure",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "verify_master_failure",
                        "type": "bool"
                    },
                    {
                        "default_value": 1,
                        "description": "Number of connection attempts to make to a server",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "backend_connect_attempts",
                        "type": "count"
                    },
                    {
                        "default_value": "3000ms",
                        "description": "Connection timeout for monitor connections",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "backend_connect_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "3000ms",
                        "description": "Read timeout for monitor connections",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "backend_read_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "3000ms",
                        "description": "Write timeout for monitor connections",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "backend_write_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "0ms",
                        "description": "How often the disk space is checked",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "disk_space_check_interval",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "description": "Disk space threshold",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "disk_space_threshold",
                        "type": "string"
                    },
                    {
                        "default_value": "all,master_down,master_up,slave_down,slave_up,server_down,server_up,synced_down,synced_up,donor_down,donor_up,lost_master,lost_slave,lost_synced,lost_donor,new_master,new_slave,new_synced,new_donor",
                        "description": "Events that cause the script to be called",
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
                        "modifiable": true,
                        "name": "events",
                        "type": "enum_mask"
                    },
                    {
                        "default_value": "28800000ms",
                        "description": "The time the on-disk cached server states are valid for",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "journal_max_age",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "2000ms",
                        "description": "How often the servers are monitored",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "monitor_interval",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "description": "Password for the user used to monitor the servers",
                        "mandatory": true,
                        "modifiable": true,
                        "name": "password",
                        "type": "password"
                    },
                    {
                        "description": "Script to run whenever an event occurs",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "script",
                        "type": "string"
                    },
                    {
                        "default_value": "90000ms",
                        "description": "Timeout for the script",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "script_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "description": "List of servers to use",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "servers",
                        "type": "serverlist"
                    },
                    {
                        "description": "Username used to monitor the servers",
                        "mandatory": true,
                        "modifiable": true,
                        "name": "user",
                        "type": "string"
                    }
                ],
                "version": "V1.5.0"
            },
            "id": "mariadbmon",
            "links": {
                "self": "http://localhost:8989/v1/modules/mariadbmon/"
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
                        "default_value": "::",
                        "description": "Listener address",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "address",
                        "type": "string"
                    },
                    {
                        "description": "Listener authenticator",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "authenticator",
                        "type": "string"
                    },
                    {
                        "description": "Authenticator options",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "authenticator_options",
                        "type": "string"
                    },
                    {
                        "description": "Path to connection initialization SQL",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "connection_init_sql_file",
                        "type": "path"
                    },
                    {
                        "default_value": 0,
                        "description": "Listener port",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "port",
                        "type": "count"
                    },
                    {
                        "default_value": "MariaDBProtocol",
                        "description": "Listener protocol to use",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "protocol",
                        "type": "module"
                    },
                    {
                        "description": "Allowed (sub)networks for proxy protocol connections. Should be a comma-separated list of IPv4 or IPv6 addresses.",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "proxy_protocol_networks",
                        "type": "string"
                    },
                    {
                        "description": "Service to which the listener connects to",
                        "mandatory": true,
                        "modifiable": false,
                        "name": "service",
                        "type": "service"
                    },
                    {
                        "description": "Listener UNIX socket",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "socket",
                        "type": "string"
                    },
                    {
                        "default_value": "default",
                        "description": "SQL parsing mode",
                        "enum_values": [
                            "default",
                            "oracle"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "sql_mode",
                        "type": "enum"
                    },
                    {
                        "default_value": false,
                        "description": "Enable TLS for server",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl",
                        "type": "bool"
                    },
                    {
                        "description": "TLS certificate authority",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_ca",
                        "type": "path"
                    },
                    {
                        "deprecated": true,
                        "description": "Alias for 'ssl_ca'",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_ca_cert",
                        "type": "path"
                    },
                    {
                        "description": "TLS public certificate",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_cert",
                        "type": "path"
                    },
                    {
                        "default_value": 9,
                        "description": "TLS certificate verification depth",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_cert_verify_depth",
                        "type": "count"
                    },
                    {
                        "description": "TLS cipher list",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_cipher",
                        "type": "string"
                    },
                    {
                        "description": "TLS certificate revocation list",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_crl",
                        "type": "string"
                    },
                    {
                        "description": "TLS private key",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_key",
                        "type": "path"
                    },
                    {
                        "default_value": false,
                        "description": "Verify TLS peer certificate",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "ssl_verify_peer_certificate",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Verify TLS peer host",
                        "mandatory": false,
                        "modifiable": true,
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
                        "modifiable": true,
                        "name": "ssl_version",
                        "type": "enum"
                    },
                    {
                        "description": "Path to user and group mapping file",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "user_mapping_file",
                        "type": "path"
                    }
                ],
                "version": "V1.1.0"
            },
            "id": "MariaDBProtocol",
            "links": {
                "self": "http://localhost:8989/v1/modules/MariaDBProtocol/"
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
                "parameters": null,
                "version": "V1.0.0"
            },
            "id": "qc_sqlite",
            "links": {
                "self": "http://localhost:8989/v1/modules/qc_sqlite/"
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
                            "self": "http://localhost:8989/v1/modules/qlafilter/log/"
                        },
                        "type": "module_command"
                    }
                ],
                "description": "A simple query logging filter",
                "maturity": "GA",
                "module_type": "Filter",
                "parameters": [
                    {
                        "default_value": true,
                        "description": "Append new entries to log files instead of overwriting them",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "append",
                        "type": "bool"
                    },
                    {
                        "default_value": "ms",
                        "description": "Duration in milliseconds (ms) or microseconds (us)",
                        "enum_values": [
                            "ms",
                            "milliseconds",
                            "us",
                            "microseconds"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "duration_unit",
                        "type": "enum"
                    },
                    {
                        "description": "Exclude queries matching this pattern from the log",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "exclude",
                        "type": "regex"
                    },
                    {
                        "description": "The basename of the output file",
                        "mandatory": true,
                        "modifiable": true,
                        "name": "filebase",
                        "type": "string"
                    },
                    {
                        "default_value": false,
                        "description": "Flush log files after every write",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "flush",
                        "type": "bool"
                    },
                    {
                        "default_value": "date,user,query",
                        "description": "Type of data to log in the log files",
                        "enum_values": [
                            "service",
                            "session",
                            "date",
                            "user",
                            "query",
                            "reply_time",
                            "total_reply_time",
                            "default_db",
                            "num_rows",
                            "reply_size",
                            "transaction",
                            "transaction_time",
                            "num_warnings",
                            "error_msg",
                            "server"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_data",
                        "type": "enum_mask"
                    },
                    {
                        "default_value": "session",
                        "description": "The type of log file to use",
                        "enum_values": [
                            "session",
                            "unified",
                            "stdout"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_type",
                        "type": "enum_mask"
                    },
                    {
                        "description": "Only log queries matching this pattern",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "match",
                        "type": "regex"
                    },
                    {
                        "default_value": " ",
                        "description": "Value used to replace newlines",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "newline_replacement",
                        "type": "string"
                    },
                    {
                        "default_value": "",
                        "description": "Regular expression options",
                        "enum_values": [
                            "case",
                            "ignorecase",
                            "extended"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "options",
                        "type": "enum_mask"
                    },
                    {
                        "default_value": ",",
                        "description": "Defines the separator between elements of a log entry",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "separator",
                        "type": "string"
                    },
                    {
                        "description": "Log queries only from this network address",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "source",
                        "type": "string"
                    },
                    {
                        "default_value": false,
                        "description": "Write queries in canonical form",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "use_canonical_form",
                        "type": "bool"
                    },
                    {
                        "description": "Log queries only from this user",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "user",
                        "type": "string"
                    }
                ],
                "version": "V1.1.1"
            },
            "id": "qlafilter",
            "links": {
                "self": "http://localhost:8989/v1/modules/qlafilter/"
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
                        "default_value": true,
                        "description": "Use master for reads",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "master_accept_reads",
                        "type": "bool"
                    },
                    {
                        "default_value": "0ms",
                        "description": "Maximum acceptable replication lag",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "max_replication_lag",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "running",
                        "description": "A comma separated list of server roles",
                        "enum_values": [
                            "master",
                            "slave",
                            "running",
                            "synced"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "router_options",
                        "type": "enum_mask"
                    },
                    {
                        "default_value": false,
                        "description": "Retrieve users from all backend servers instead of only one",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "auth_all_servers",
                        "type": "bool"
                    },
                    {
                        "default_value": "300000ms",
                        "description": "How often idle connections are pinged",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "connection_keepalive",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "0ms",
                        "description": "Connection idle timeout",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "connection_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": false,
                        "description": "Disable session command history",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "disable_sescmd_history",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Allow the root user to connect to this service",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "enable_root_user",
                        "type": "bool"
                    },
                    {
                        "default_value": "-1ms",
                        "description": "Put connections into pool after session has been idle for this long",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "idle_session_pool_time",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": true,
                        "description": "Match localhost to wildcard host",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "localhost_match_wildcard_host",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "description": "Log a warning when client authentication fails",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_auth_warnings",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Log debug messages for this service (debug builds only)",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_debug",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Log info messages for this service",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_info",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Log notice messages for this service",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_notice",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Log warning messages for this service",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_warning",
                        "type": "bool"
                    },
                    {
                        "default_value": 0,
                        "description": "Maximum number of connections",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "max_connections",
                        "type": "count"
                    },
                    {
                        "default_value": 50,
                        "description": "Session command history size",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "max_sescmd_history",
                        "type": "count"
                    },
                    {
                        "default_value": "60000ms",
                        "description": "How long a session can wait for a connection to become available",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "multiplex_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "0ms",
                        "description": "Network write timeout",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "net_write_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "description": "Password for the user used to retrieve database users",
                        "mandatory": true,
                        "modifiable": true,
                        "name": "password",
                        "type": "password"
                    },
                    {
                        "default_value": true,
                        "description": "Prune old session command history if the limit is exceeded",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "prune_sescmd_history",
                        "type": "bool"
                    },
                    {
                        "default_value": "primary",
                        "description": "Service rank",
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
                        "default_value": -1,
                        "description": "Number of statements kept in memory",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "retain_last_statements",
                        "type": "int"
                    },
                    {
                        "default_value": false,
                        "description": "Enable session tracing for this service",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "session_trace",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Track session state using server responses",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "session_track_trx_state",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "description": "Strip escape characters from database names",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "strip_db_esc",
                        "type": "bool"
                    },
                    {
                        "description": "Username used to retrieve database users",
                        "mandatory": true,
                        "modifiable": true,
                        "name": "user",
                        "type": "string"
                    },
                    {
                        "description": "Load additional users from a file",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "user_accounts_file",
                        "type": "path"
                    },
                    {
                        "default_value": "add_when_load_ok",
                        "description": "When and how the user accounts file is used",
                        "enum_values": [
                            "add_when_load_ok",
                            "file_only_always"
                        ],
                        "mandatory": false,
                        "modifiable": false,
                        "name": "user_accounts_file_usage",
                        "type": "enum"
                    },
                    {
                        "description": "Custom version string to use",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "version_string",
                        "type": "string"
                    }
                ],
                "version": "V2.0.0"
            },
            "id": "readconnroute",
            "links": {
                "self": "http://localhost:8989/v1/modules/readconnroute/"
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
                        "description": "Causal reads mode",
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
                            "fast_global",
                            "fast",
                            "universal"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "causal_reads",
                        "type": "enum"
                    },
                    {
                        "default_value": "10000ms",
                        "description": "Timeout for the slave synchronization",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "causal_reads_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": false,
                        "description": "Retry failed writes outside of transactions",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "delayed_retry",
                        "type": "bool"
                    },
                    {
                        "default_value": "10000ms",
                        "description": "Timeout for delayed_retry",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "delayed_retry_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": false,
                        "description": "Create connections only when needed",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "lazy_connect",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Use master for reads",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "master_accept_reads",
                        "type": "bool"
                    },
                    {
                        "default_value": "fail_instantly",
                        "description": "Master failure mode behavior",
                        "enum_values": [
                            "fail_instantly",
                            "fail_on_write",
                            "error_on_write"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "master_failure_mode",
                        "type": "enum"
                    },
                    {
                        "default_value": false,
                        "description": "Reconnect to master",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "master_reconnection",
                        "type": "bool"
                    },
                    {
                        "default_value": 255,
                        "description": "Maximum number of slave connections",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "max_slave_connections",
                        "type": "count"
                    },
                    {
                        "default_value": "0ms",
                        "description": "Maximum allowed slave replication lag",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "max_slave_replication_lag",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": false,
                        "description": "Optimistically offload transactions to slaves",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "optimistic_trx",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "description": "Automatically retry failed reads outside of transactions",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "retry_failed_reads",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Reuse identical prepared statements inside the same connection",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "reuse_prepared_statements",
                        "type": "bool"
                    },
                    {
                        "default_value": 255,
                        "description": "Starting number of slave connections",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "slave_connections",
                        "type": "count"
                    },
                    {
                        "default_value": "LEAST_CURRENT_OPERATIONS",
                        "description": "Slave selection criteria",
                        "enum_values": [
                            "LEAST_GLOBAL_CONNECTIONS",
                            "LEAST_ROUTER_CONNECTIONS",
                            "LEAST_BEHIND_MASTER",
                            "LEAST_CURRENT_OPERATIONS",
                            "ADAPTIVE_ROUTING"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "slave_selection_criteria",
                        "type": "enum"
                    },
                    {
                        "default_value": false,
                        "description": "Lock connection to master after multi-statement query",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "strict_multi_stmt",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Lock connection to master after a stored procedure is executed",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "strict_sp_calls",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Retry failed transactions",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "transaction_replay",
                        "type": "bool"
                    },
                    {
                        "default_value": 5,
                        "description": "Maximum number of times to retry a transaction",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "transaction_replay_attempts",
                        "type": "count"
                    },
                    {
                        "default_value": "full",
                        "description": "Type of checksum to calculate for results",
                        "enum_values": [
                            "full",
                            "result_only",
                            "no_insert_id"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "transaction_replay_checksum",
                        "type": "enum"
                    },
                    {
                        "default_value": 1073741824,
                        "description": "Maximum size of transaction to retry",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "transaction_replay_max_size",
                        "type": "size"
                    },
                    {
                        "default_value": false,
                        "description": "Retry transaction on deadlock",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "transaction_replay_retry_on_deadlock",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Retry transaction on checksum mismatch",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "transaction_replay_retry_on_mismatch",
                        "type": "bool"
                    },
                    {
                        "default_value": "0ms",
                        "description": "Timeout for transaction replay",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "transaction_replay_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "all",
                        "description": "Whether to route SQL variable modifications to all servers or only to the master",
                        "enum_values": [
                            "all",
                            "master"
                        ],
                        "mandatory": false,
                        "modifiable": true,
                        "name": "use_sql_variables_in",
                        "type": "enum"
                    },
                    {
                        "default_value": false,
                        "description": "Retrieve users from all backend servers instead of only one",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "auth_all_servers",
                        "type": "bool"
                    },
                    {
                        "default_value": "300000ms",
                        "description": "How often idle connections are pinged",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "connection_keepalive",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "0ms",
                        "description": "Connection idle timeout",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "connection_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": false,
                        "description": "Disable session command history",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "disable_sescmd_history",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Allow the root user to connect to this service",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "enable_root_user",
                        "type": "bool"
                    },
                    {
                        "default_value": "-1ms",
                        "description": "Put connections into pool after session has been idle for this long",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "idle_session_pool_time",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": true,
                        "description": "Match localhost to wildcard host",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "localhost_match_wildcard_host",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "description": "Log a warning when client authentication fails",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_auth_warnings",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Log debug messages for this service (debug builds only)",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_debug",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Log info messages for this service",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_info",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Log notice messages for this service",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_notice",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Log warning messages for this service",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "log_warning",
                        "type": "bool"
                    },
                    {
                        "default_value": 0,
                        "description": "Maximum number of connections",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "max_connections",
                        "type": "count"
                    },
                    {
                        "default_value": 50,
                        "description": "Session command history size",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "max_sescmd_history",
                        "type": "count"
                    },
                    {
                        "default_value": "60000ms",
                        "description": "How long a session can wait for a connection to become available",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "multiplex_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "default_value": "0ms",
                        "description": "Network write timeout",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "net_write_timeout",
                        "type": "duration",
                        "unit": "ms"
                    },
                    {
                        "description": "Password for the user used to retrieve database users",
                        "mandatory": true,
                        "modifiable": true,
                        "name": "password",
                        "type": "password"
                    },
                    {
                        "default_value": true,
                        "description": "Prune old session command history if the limit is exceeded",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "prune_sescmd_history",
                        "type": "bool"
                    },
                    {
                        "default_value": "primary",
                        "description": "Service rank",
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
                        "default_value": -1,
                        "description": "Number of statements kept in memory",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "retain_last_statements",
                        "type": "int"
                    },
                    {
                        "default_value": false,
                        "description": "Enable session tracing for this service",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "session_trace",
                        "type": "bool"
                    },
                    {
                        "default_value": false,
                        "description": "Track session state using server responses",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "session_track_trx_state",
                        "type": "bool"
                    },
                    {
                        "default_value": true,
                        "description": "Strip escape characters from database names",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "strip_db_esc",
                        "type": "bool"
                    },
                    {
                        "description": "Username used to retrieve database users",
                        "mandatory": true,
                        "modifiable": true,
                        "name": "user",
                        "type": "string"
                    },
                    {
                        "description": "Load additional users from a file",
                        "mandatory": false,
                        "modifiable": false,
                        "name": "user_accounts_file",
                        "type": "path"
                    },
                    {
                        "default_value": "add_when_load_ok",
                        "description": "When and how the user accounts file is used",
                        "enum_values": [
                            "add_when_load_ok",
                            "file_only_always"
                        ],
                        "mandatory": false,
                        "modifiable": false,
                        "name": "user_accounts_file_usage",
                        "type": "enum"
                    },
                    {
                        "description": "Custom version string to use",
                        "mandatory": false,
                        "modifiable": true,
                        "name": "version_string",
                        "type": "string"
                    }
                ],
                "version": "V1.1.0"
            },
            "id": "readwritesplit",
            "links": {
                "self": "http://localhost:8989/v1/modules/readwritesplit/"
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

Here is an example POST requests to the mariadbmon module command _reset-replication_ with
two parameters, the name of the monitor instance and the server name:

```
POST /v1/maxscale/modules/mariadbmon/reset-replication?MariaDB-Monitor&server1
```

#### Response

Command with output:

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/modules/mariadbmon/reset-replication"
    },
    "meta": [ // Output of module command (module dependent)
        {
            "name": "value"
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
            "canonical": "SELECT ?",
            "fields": [],
            "functions": [],
            "operation": "QUERY_OP_SELECT",
            "parse_result": "QC_QUERY_PARSED",
            "type_mask": "QUERY_TYPE_READ"
        },
        "id": "classify",
        "type": "classify"
    },
    "links": {
        "self": "http://localhost:8989/v1/maxscale/query_classifier/classify/"
    }
}
```
