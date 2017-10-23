# Monitor Resource

A monitor resource represents a monitor inside MaxScale that monitors one or
more servers.

## Resource Operations

### Get a monitor

Get a single monitor. The _:name_ in the URI must be a valid monitor name with
all whitespace replaced with hyphens. The monitor names are case-sensitive.

```
GET /v1/monitors/:name
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/monitors/MySQL-Monitor"
    },
    "data": {
        "id": "MySQL-Monitor",
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
            "module": "mysqlmon",
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
            "self": "http://localhost:8989/v1/monitors/MySQL-Monitor"
        }
    }
}
```

### Get all monitors

Get all monitors.

```
GET /v1/monitors
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/monitors/"
    },
    "data": [
        {
            "id": "MySQL-Monitor",
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
                "module": "mysqlmon",
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
                "self": "http://localhost:8989/v1/monitors/MySQL-Monitor"
            }
        }
    ]
}
```

### Create a monitor

Create a new monitor. The request body must define the `/data/id`
field with the name of the monitor, the `/data/type` field with the
value of `monitors` and the `/data/attributes/module` field with the
monitor module for this monitor. All of the monitor parameters can
be defined at creation time.

```
POST /v1/monitors
```

The following example defines a request body which creates the new monitor,
_test-monitor_, and assigns two servers to be monitored by it. It also defines
a custom value for the _monitor_interval_ parameter.

```javascript
{
    data: {
        "id": "test-monitor", // Name of the monitor
        "type": "monitors",
        "attributes": {
            "module": "mysqlmon", // The monitor uses the mysqlmon module
            "parameters": { // Monitor parameters
                "monitor_interval": 1000
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

The :name in the URI must map to a monitor name with all whitespace replaced with
hyphens. The request body must be a valid JSON document representing the modified monitor.

```
PATCH /v1/monitor/:name
```

### Modifiable Fields

The following standard server parameter can be modified.
- [user](../Monitors/Monitor-Common.md#user)
- [password](../Monitors/Monitor-Common.md#password)
- [monitor_interval](../Monitors/Monitor-Common.md#monitor_interval)
- [backend_connect_timeout](../Monitors/Monitor-Common.md#backend_connect_timeout)
- [backend_write_timeout](../Monitors/Monitor-Common.md#backend_write_timeout)
- [backend_read_timeout](../Monitors/Monitor-Common.md#backend_read_timeout)
- [backend_connect_attempts](../Monitors/Monitor-Common.md#backend_connect_attempts)

Refer to the documentation on these parameters for valid values.

In addition to these standard parameters, the monitor specific parameters can also be
modified. Refer to the monitor module documentation for details on these parameters.

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

Destroy a created monitor. The monitor must not have relationships to any
servers and if it does a PATCH request which removes these relationships is
required before a DELETE request for the monitor can be made.

```
DELETE /v1/monitor/:name/stop
```

#### Response

Monitor is deleted:

`Status: 204 No Content`

Monitor could not be deleted:

`Status: 403 Forbidden`

### Stop a monitor

Stops a started monitor.

```
PUT /v1/monitor/:name/stop
```

#### Response

Monitor is stopped:

`Status: 204 No Content`

### Start a monitor

Starts a stopped monitor.

```
PUT /v1/monitor/:name/start
```

#### Response

Monitor is started:

`Status: 204 No Content`
