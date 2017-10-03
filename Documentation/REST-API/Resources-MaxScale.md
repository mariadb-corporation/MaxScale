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
                "thread_stack_size": 8388608,
                "auth_connect_timeout": 3,
                "auth_read_timeout": 1,
                "auth_write_timeout": 2,
                "skip_permission_checks": false,
                "admin_auth": false,
                "admin_enabled": true,
                "admin_log_auth_failures": true,
                "admin_host": "::",
                "admin_port": 8989,
                "admin_ssl_key": "",
                "admin_ssl_cert": "",
                "admin_ssl_ca_cert": "",
                "query_classifier": ""
            },
            "version": "2.2.0",
            "commit": "aa1a413cd961d467083d1974c2a027f612201845",
            "started_at": "Wed, 06 Sep 2017 06:51:54 GMT",
            "uptime": 1227
        },
        "id": "maxscale",
        "type": "maxscale"
    }
}
```

## Update MaxScale parameters

Update MaxScale parameters. The request body must define updated values for the
`data.attributes.parameters` object. The following parameters can be altered:

- [admin_auth](../Getting-Started/Configuration-Guide.md#admin_auth)
- [auth_connect_timeout](../Getting-Started/Configuration-Guide.md#auth_connect_timeout)
- [auth_read_timeout](../Getting-Started/Configuration-Guide.md#auth_read_timeout)
- [auth_write_timeout](../Getting-Started/Configuration-Guide.md#auth_write_timeout)
- [admin_log_auth_failures](../Getting-Started/Configuration-Guide.md#admin_log_auth_failures)

```
PATCH /v1/maxscale
```

#### Response

Parameters modified:

`Status: 204 No Content`

Invalid JSON body:

`Status: 403 Forbidden`

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
                "log_info": true,
                "log_debug": false,
                "log_to_shm": false
            },
            "log_file": "/home/markusjm/build/log/maxscale/maxscale.log",
            "log_priorities": [
                "error",
                "warning",
                "notice",
                "info"
            ]
        },
        "id": "logs",
        "type": "logs"
    }
}
```

## Update logging parameters

Update logging parameters. The request body must define updated values for the
`data.attributes.parameters` object. All logging parameters apart from
`log_to_shm` can be altered at runtime.

```
PATCH /v1/maxscale/logs
```

#### Response

Parameters modified:

`Status: 204 No Content`

Invalid JSON body:

`Status: 403 Forbidden`

## Flush and rotate log files

Flushes any pending messages to disk and reopens the log files. The body of the
message is ignored.

```
POST /v1/maxscale/logs/flush
```

#### Response

`Status: 204 No Content`

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
        "id": "dbfwfilter",
        "type": "module",
        "attributes": {
            "module_type": "Filter",
            "version": "V1.2.0",
            "description": "Firewall Filter",
            "api": "filter",
            "status": "GA",
            "commands": [
                {
                    "id": "rules/reload",
                    "type": "module_command",
                    "links": {
                        "self": "http://localhost:8989/v1/modules/dbfwfilter/rules/reload"
                    },
                    "attributes": {
                        "method": "POST",
                        "arg_min": 1,
                        "arg_max": 2,
                        "parameters": [
                            {
                                "description": "Filter to reload",
                                "type": "FILTER",
                                "required": true
                            },
                            {
                                "description": "Path to rule file",
                                "type": "[STRING]",
                                "required": false
                            }
                        ]
                    }
                }
            ],
            "parameters": [
                {
                    "name": "rules",
                    "type": "path"
                },
                {
                    "name": "log_match",
                    "type": "bool",
                    "default_value": "false"
                },
                {
                    "name": "log_no_match",
                    "type": "bool",
                    "default_value": "false"
                },
                {
                    "name": "action",
                    "type": "enum",
                    "default_value": "block",
                    "enum_values": [
                        "allow",
                        "block",
                        "ignore"
                    ]
                }
            ]
        },
        "links": {
            "self": "http://localhost:8989/v1/modules/dbfwfilter"
        }
    }
}
```

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

## Call a module command

Modules can expose commands that can be called via the REST API. The module
resource lists all commands in the `data.attributes.commands` list. Each value
is a command sub-resource identified by its `id` field and the HTTP method the
command uses is defined by the `attributes.method` field.

The _:module_ in the URI must be a valid name of a loaded module and _:command_
must be a valid command identifier that is exposed by that module. All
parameters to the module commands are passed as HTTP request parameters.

For read-only commands:

```
GET /v1/maxscale/modules/:module/:command
```

For commands that can modify data:

```
POST /v1/maxscale/modules/:module/:command
```

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
    "meta": [
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
