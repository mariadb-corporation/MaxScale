# MaxScale Resource

The MaxScale resource represents a MaxScale instance and it is the core on top
of which the modules build upon.

## Resource Operations

## Get global information

Retrieve global information about a MaxScale instance. This includes various
file locations, configuration options and version information.

```
GET /v1/maxscale
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/"
    },
    "data": {
        "attributes": {
            "parameters": {
                "libdir": "/usr/lib64/maxscale",
                "datadir": "/var/lib/maxscale",
                "process_datadir": "/var/lib/maxscale/data16218",
                "cachedir": "/var/cache/maxscale",
                "configdir": "/etc",
                "config_persistdir": "/var/lib/maxscale/maxscale.cnf.d",
                "module_configdir": "/etc/maxscale.modules.d",
                "piddir": "/var/run/maxscale",
                "logdir": "/var/log/maxscale",
                "langdir": "/var/lib/maxscale",
                "execdir": "/usr/bin",
                "connector_plugindir": "/var/lib/plugin",
                "threads": 4,
                "auth_connect_timeout": 3,
                "auth_read_timeout": 1,
                "auth_write_timeout": 2,
                "skip_permission_checks": false,
                "syslog": true,
                "maxlog": true,
                "log_to_shm": false,
                "query_classifier": ""
            },
            "version": "2.1.3",
            "commit": "a32aa6c16236d2d8830e1286ea3aa4dba19174ec",
            "started_at": "Wed, 17 May 2017 05:33:46 GMT",
            "uptime": 19
        },
        "id": "maxscale",
        "type": "maxscale"
    }
}
```

#### Supported Request Parameter

- `pretty`

## Get thread information

Get the information and statistics of a particular thread. The _:id_ in
the URI must map to a valid thread number between 0 and the configured
value of `threads`.

```
GET /v1/maxscale/threads/:id
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/threads/0"
    },
    "data": {
        "id": "0",
        "type": "threads",
        "attributes": {
            "stats": {
                "reads": 2,
                "writes": 0,
                "errors": 0,
                "hangups": 0,
                "accepts": 0,
                "blocking_polls": 180,
                "event_queue_length": 1,
                "max_event_queue_length": 1,
                "max_exec_time": 0,
                "max_queue_time": 0
            }
        },
        "links": {
            "self": "http://localhost:8989/v1/threads/0"
        }
    }
}
```

#### Supported Request Parameter

- `pretty`

## Get information for all threads

Get the informatino for all threads. Returns a collection of threads resources.

```
GET /v1/maxscale/threads
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/threads/"
    },
    "data": [
        {
            "id": "0",
            "type": "threads",
            "attributes": {
                "stats": {
                    "reads": 1,
                    "writes": 0,
                    "errors": 0,
                    "hangups": 0,
                    "accepts": 0,
                    "blocking_polls": 116,
                    "event_queue_length": 1,
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0
                }
            },
            "links": {
                "self": "http://localhost:8989/v1/threads/0"
            }
        },
        {
            "id": "1",
            "type": "threads",
            "attributes": {
                "stats": {
                    "reads": 1,
                    "writes": 0,
                    "errors": 0,
                    "hangups": 0,
                    "accepts": 0,
                    "blocking_polls": 116,
                    "event_queue_length": 1,
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0
                }
            },
            "links": {
                "self": "http://localhost:8989/v1/threads/1"
            }
        },
        {
            "id": "2",
            "type": "threads",
            "attributes": {
                "stats": {
                    "reads": 1,
                    "writes": 0,
                    "errors": 0,
                    "hangups": 0,
                    "accepts": 0,
                    "blocking_polls": 116,
                    "event_queue_length": 1,
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0
                }
            },
            "links": {
                "self": "http://localhost:8989/v1/threads/2"
            }
        },
        {
            "id": "3",
            "type": "threads",
            "attributes": {
                "stats": {
                    "reads": 1,
                    "writes": 0,
                    "errors": 0,
                    "hangups": 0,
                    "accepts": 0,
                    "blocking_polls": 116,
                    "event_queue_length": 1,
                    "max_event_queue_length": 1,
                    "max_exec_time": 0,
                    "max_queue_time": 0
                }
            },
            "links": {
                "self": "http://localhost:8989/v1/threads/3"
            }
        }
    ]
}
```

#### Supported Request Parameter

- `pretty`

## Get logging information

Get information about the current state of logging, enabled log files and the
location where the log files are stored.

```
GET /v1/maxscale/logs
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/logs/"
    },
    "data": {
        "attributes": {
            "parameters": {
                "highprecision": false,
                "maxlog": true,
                "syslog": true,
                "throttling": {
                    "count": 10,
                    "suppress_ms": 10000,
                    "window_ms": 1000
                },
                "log_warning": true,
                "log_notice": true,
                "log_info": false,
                "log_debug": false
            }
        },
        "id": "logs",
        "type": "logs"
    }
}
```

#### Supported Request Parameter

- `pretty`

## Flush and rotate log files

Flushes any pending messages to disk and reopens the log files. The body of the
message is ignored.

```
POST /v1/maxscale/logs/flush
```

#### Response

```
Status: 204 No Content
```

## Get task schedule

Retrieve all pending tasks that are queued for execution.

```
GET /v1/maxscale/tasks
```

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

#### Supported Request Parameter

- `pretty`

## Get loaded modules

Retrieve information about a loaded module. This includes version, API and
maturity information as well as all the parameters that the module defines.

```
GET /v1/maxscale/modules
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/modules/"
    },
    "data": {
        "id": "readwritesplit",
        "type": "module",
        "attributes": {
            "module_type": "Router",
            "version": "V1.1.0",
            "description": "A Read/Write splitting router for enhancement read scalability",
            "api": "router",
            "status": "GA",
            "parameters": [
                {
                    "name": "use_sql_variables_in",
                    "type": "enum",
                    "default_value": "all",
                    "enum_values": [
                        "all",
                        "master"
                    ]
                },
                {
                    "name": "slave_selection_criteria",
                    "type": "enum",
                    "default_value": "LEAST_CURRENT_OPERATIONS",
                    "enum_values": [
                        "LEAST_GLOBAL_CONNECTIONS",
                        "LEAST_ROUTER_CONNECTIONS",
                        "LEAST_BEHIND_MASTER",
                        "LEAST_CURRENT_OPERATIONS"
                    ]
                },
                {
                    "name": "master_failure_mode",
                    "type": "enum",
                    "default_value": "fail_instantly",
                    "enum_values": [
                        "fail_instantly",
                        "fail_on_write",
                        "error_on_write"
                    ]
                },
                {
                    "name": "max_slave_replication_lag",
                    "type": "int",
                    "default_value": "-1"
                },
                {
                    "name": "max_slave_connections",
                    "type": "string",
                    "default_value": "255"
                },
                {
                    "name": "retry_failed_reads",
                    "type": "bool",
                    "default_value": "true"
                },
                {
                    "name": "disable_sescmd_history",
                    "type": "bool",
                    "default_value": "true"
                },
                {
                    "name": "max_sescmd_history",
                    "type": "count",
                    "default_value": "0"
                },
                {
                    "name": "strict_multi_stmt",
                    "type": "bool",
                    "default_value": "true"
                },
                {
                    "name": "master_accept_reads",
                    "type": "bool",
                    "default_value": "false"
                },
                {
                    "name": "connection_keepalive",
                    "type": "count",
                    "default_value": "0"
                }
            ]
        },
        "links": {
            "self": "http://localhost:8989/v1/modules/readwritesplit"
        }
    }
}
```

#### Supported Request Parameter

- `pretty`

## Get all loaded modules

Retrieve information about all loaded modules.

```
GET /v1/maxscale/modules
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/modules/"
    },
    "data": [
        {
            "id": "qc_sqlite",
            "type": "module",
            "attributes": {
                "module_type": "QueryClassifier",
                "version": "V1.0.0",
                "description": "Query classifier using sqlite.",
                "api": "query_classifier",
                "status": "Beta",
                "parameters": []
            },
            "links": {
                "self": "http://localhost:8989/v1/modules/qc_sqlite"
            }
        },
        {
            "id": "MySQLAuth",
            "type": "module",
            "attributes": {
                "module_type": "Authenticator",
                "version": "V1.1.0",
                "description": "The MySQL client to MaxScale authenticator implementation",
                "api": "authenticator",
                "status": "GA",
                "parameters": []
            },
            "links": {
                "self": "http://localhost:8989/v1/modules/MySQLAuth"
            }
        },
    ]
}
```

#### Supported Request Parameter

- `pretty`
