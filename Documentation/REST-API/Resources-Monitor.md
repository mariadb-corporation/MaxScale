# Monitor Resource

A monitor resource represents a monitor inside MaxScale that monitors one or
more servers.

## Resource Operations

### Get a monitor

```
GET /v1/monitors/:name
```

Get a single monitor. The _:name_ in the URI must be a valid monitor name with
all whitespace replaced with hyphens. The monitor names are case-sensitive.

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/monitors/MariaDB-Monitor"
    },
    "data": {
        "id": "MariaDB-Monitor",
        "type": "monitors",
        "relationships": {
            "servers": {
                "links": {
                    "self": "http://localhost:8989/v1/servers/"
                },
                "data": [
                    {
                        "id": "server1",
                        "type": "servers"
                    },
                    {
                        "id": "server2",
                        "type": "servers"
                    }
                ]
            }
        },
        "attributes": {
            "module": "mariadbmon",
            "state": "Running",
            "parameters": {
                "user": "maxuser",
                "password": "maxpwd",
                "monitor_interval": 10000,
                "backend_connect_timeout": 3,
                "backend_read_timeout": 1,
                "backend_write_timeout": 2,
                "backend_connect_attempts": 1,
                "detect_replication_lag": false,
                "detect_stale_master": true,
                "detect_stale_slave": true,
                "mysql51_replication": false,
                "multimaster": false,
                "detect_standalone_master": false,
                "failcount": 5,
                "allow_cluster_recovery": true,
                "journal_max_age": 28800
            },
            "monitor_diagnostics": {
                "monitor_id": 0,
                "detect_stale_master": true,
                "detect_stale_slave": true,
                "detect_replication_lag": false,
                "multimaster": false,
                "detect_standalone_master": false,
                "failcount": 5,
                "allow_cluster_recovery": true,
                "mysql51_replication": false,
                "journal_max_age": 28800,
                "server_info": [
                    {
                        "name": "server1",
                        "server_id": 0,
                        "master_id": 0,
                        "read_only": false,
                        "slave_configured": false,
                        "slave_io_running": false,
                        "slave_sql_running": false,
                        "master_binlog_file": "",
                        "master_binlog_position": 0
                    },
                    {
                        "name": "server2",
                        "server_id": 0,
                        "master_id": 0,
                        "read_only": false,
                        "slave_configured": false,
                        "slave_io_running": false,
                        "slave_sql_running": false,
                        "master_binlog_file": "",
                        "master_binlog_position": 0
                    }
                ]
            }
        },
        "links": {
            "self": "http://localhost:8989/v1/monitors/MariaDB-Monitor"
        }
    }
}
```

### Get all monitors

```
GET /v1/monitors
```

Get all monitors.

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/monitors/"
    },
    "data": [
        {
            "id": "MariaDB-Monitor",
            "type": "monitors",
            "relationships": {
                "servers": {
                    "links": {
                        "self": "http://localhost:8989/v1/servers/"
                    },
                    "data": [
                        {
                            "id": "server1",
                            "type": "servers"
                        },
                        {
                            "id": "server2",
                            "type": "servers"
                        }
                    ]
                }
            },
            "attributes": {
                "module": "mariadbmon",
                "state": "Running",
                "parameters": {
                    "user": "maxuser",
                    "password": "maxpwd",
                    "monitor_interval": 10000,
                    "backend_connect_timeout": 3,
                    "backend_read_timeout": 1,
                    "backend_write_timeout": 2,
                    "backend_connect_attempts": 1,
                    "detect_replication_lag": false,
                    "detect_stale_master": true,
                    "detect_stale_slave": true,
                    "mysql51_replication": false,
                    "multimaster": false,
                    "detect_standalone_master": false,
                    "failcount": 5,
                    "allow_cluster_recovery": true,
                    "journal_max_age": 28800
                },
                "monitor_diagnostics": {
                    "monitor_id": 0,
                    "detect_stale_master": true,
                    "detect_stale_slave": true,
                    "detect_replication_lag": false,
                    "multimaster": false,
                    "detect_standalone_master": false,
                    "failcount": 5,
                    "allow_cluster_recovery": true,
                    "mysql51_replication": false,
                    "journal_max_age": 28800,
                    "server_info": [
                        {
                            "name": "server1",
                            "server_id": 0,
                            "master_id": 0,
                            "read_only": false,
                            "slave_configured": false,
                            "slave_io_running": false,
                            "slave_sql_running": false,
                            "master_binlog_file": "",
                            "master_binlog_position": 0
                        },
                        {
                            "name": "server2",
                            "server_id": 0,
                            "master_id": 0,
                            "read_only": false,
                            "slave_configured": false,
                            "slave_io_running": false,
                            "slave_sql_running": false,
                            "master_binlog_file": "",
                            "master_binlog_position": 0
                        }
                    ]
                }
            },
            "links": {
                "self": "http://localhost:8989/v1/monitors/MariaDB-Monitor"
            }
        }
    ]
}
```

### Create a monitor

```
POST /v1/monitors
```

Create a new monitor. The request body must define at least the following
fields.

* `data.id`
  * Name of the monitor

* `data.type`
  * Type of the object, must be `monitors`

* `data.attributes.module`
  * The monitor module to use

* `data.attributes.parameters.user`
  * The [`user`](../Getting-Started/Configuration-Guide.md#password) to use

* `data.attributes.parameters.password`
  * The [`password`](../Getting-Started/Configuration-Guide.md#password) to use

All monitor parameters can be defined at creation time.

The following example defines a request body which creates a new monitor and
assigns two servers to be monitored by it. It also defines a custom value for
the _monitor_interval_ parameter.

```javascript
{
    data: {
        "id": "test-monitor", // Name of the monitor
        "type": "monitors",
        "attributes": {
            "module": "mariadbmon", // The monitor uses the mariadbmon module
            "parameters": { // Monitor parameters
                "monitor_interval": 1000,
                "user": "maxuser,
                "password": "maxpwd"
            }
        },
        "relationships": { // List of server relationships that this monitor uses
            "servers": {
                "data": [ // This monitor uses two servers
                    {
                        "id": "server1",
                        "type": "servers"
                    },
                    {
                        "id": "server2",
                        "type": "servers"
                    }
                ]
            }
        }
    }
}
```

#### Response

Monitor is created:

`Status: 204 No Content`

### Update a monitor

```
PATCH /v1/monitors/:name
```

The :name in the URI must map to a monitor name with all whitespace replaced
with hyphens. The request body must be a valid JSON document representing the
modified monitor.

### Modifiable Fields

The following standard server parameter can be modified.

- [user](../Monitors/Monitor-Common.md#user)
- [password](../Monitors/Monitor-Common.md#password)
- [monitor_interval](../Monitors/Monitor-Common.md#monitor_interval)
- [backend_connect_timeout](../Monitors/Monitor-Common.md#backend_connect_timeout)
- [backend_write_timeout](../Monitors/Monitor-Common.md#backend_write_timeout)
- [backend_read_timeout](../Monitors/Monitor-Common.md#backend_read_timeout)
- [backend_connect_attempts](../Monitors/Monitor-Common.md#backend_connect_attempts)

In addition to these standard parameters, the monitor specific parameters can
also be modified. Refer to the monitor module documentation for details on these
parameters.

#### Response

Monitor is modified:

`Status: 204 No Content`

Invalid request body:

`Status: 403 Forbidden`

### Update monitor relationships

```
PATCH /v1/monitors/:name/relationships/servers
```

The _:name_ in the URI must map to a monitor name with all whitespace replaced
with hyphens.

The request body must be a JSON object that defines only the _data_ field. The
value of the _data_ field must be an array of relationship objects that define
the _id_ and _type_ fields of the relationship. This object will replace the
existing relationships of the monitor.

The following is an example request and request body that defines a single
server relationship for a monitor.

```
PATCH /v1/monitors/my-monitor/relationships/servers

{
    data: [
          { "id": "my-server", "type": "servers" }
    ]
}
```

All relationships for a monitor can be deleted by sending an empty array as the
_data_ field value. The following example removes all servers from a monitor.

```
PATCH /v1/monitors/my-monitor/relationships/servers

{
    data: []
}
```

#### Response

Monitor relationships modified:

`Status: 204 No Content`

Invalid JSON body:

`Status: 403 Forbidden`

### Destroy a monitor

```
DELETE /v1/monitors/:name/stop
```

Destroy a created monitor. The monitor must not have relationships to any
servers in order to be destroyed.

#### Response

Monitor is deleted:

`Status: 204 No Content`

Monitor could not be deleted:

`Status: 403 Forbidden`

### Stop a monitor

```
PUT /v1/monitors/:name/stop
```

Stops a started monitor.

#### Response

Monitor is stopped:

`Status: 204 No Content`

### Start a monitor

```
PUT /v1/monitors/:name/start
```

Starts a stopped monitor.

#### Response

Monitor is started:

`Status: 204 No Content`
