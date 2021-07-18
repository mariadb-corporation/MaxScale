# Monitor Resource

A monitor resource represents a monitor inside MaxScale that monitors one or
more servers.

[TOC]

## Resource Operations

The _:name_ in all of the URIs must be the name of a monitor in MaxScale.

### Get a monitor

```
GET /v1/monitors/:name
```

Get a single monitor.

#### Response

`Status: 200 OK`

```javascript
{
    "data": {
        "attributes": {
            "module": "mariadbmon",
            "monitor_diagnostics": {
                "master": "server1",
                "master_gtid_domain_id": 0,
                "primary": null,
                "server_info": [
                    {
                        "gtid_binlog_pos": "0-3000-9",
                        "gtid_current_pos": "0-3000-9",
                        "lock_held": null,
                        "master_group": null,
                        "name": "server1",
                        "read_only": false,
                        "server_id": 3000,
                        "slave_connections": []
                    },
                    {
                        "gtid_binlog_pos": "0-3000-9",
                        "gtid_current_pos": "0-3000-9",
                        "lock_held": null,
                        "master_group": null,
                        "name": "server2",
                        "read_only": false,
                        "server_id": 3001,
                        "slave_connections": [
                            {
                                "connection_name": "",
                                "gtid_io_pos": "",
                                "last_io_error": "",
                                "last_sql_error": "",
                                "master_host": "127.0.0.1",
                                "master_port": 3000,
                                "master_server_id": 3000,
                                "seconds_behind_master": 0,
                                "slave_io_running": "Yes",
                                "slave_sql_running": "Yes"
                            }
                        ]
                    }
                ],
                "state": "Idle"
            },
            "parameters": {
                "assume_unique_hostnames": true,
                "auto_failover": false,
                "auto_rejoin": false,
                "backend_connect_attempts": 1,
                "backend_connect_timeout": 3,
                "backend_read_timeout": 3,
                "backend_write_timeout": 3,
                "cooperative_monitoring_locks": "none",
                "demotion_sql_file": null,
                "detect_replication_lag": false,
                "detect_stale_master": null,
                "detect_stale_slave": null,
                "detect_standalone_master": null,
                "disk_space_check_interval": 0,
                "disk_space_threshold": null,
                "enforce_read_only_slaves": false,
                "enforce_simple_topology": false,
                "enforce_writable_master": false,
                "events": "all",
                "failcount": 5,
                "failover_timeout": 90,
                "handle_events": true,
                "ignore_external_masters": false,
                "journal_max_age": 28800,
                "maintenance_on_low_disk_space": true,
                "master_conditions": "primary_monitor_master",
                "master_failure_timeout": 10,
                "module": "mariadbmon",
                "monitor_interval": 5000,
                "password": "*****",
                "promotion_sql_file": null,
                "replication_master_ssl": false,
                "replication_password": null,
                "replication_user": null,
                "script": null,
                "script_max_replication_lag": -1,
                "script_timeout": 90,
                "servers_no_promotion": null,
                "slave_conditions": "none",
                "switchover_on_low_disk_space": false,
                "switchover_timeout": 90,
                "user": "maxuser",
                "verify_master_failure": true
            },
            "state": "Running",
            "ticks": 3
        },
        "id": "MariaDB-Monitor",
        "links": {
            "self": "http://localhost:8989/v1/monitors/MariaDB-Monitor/"
        },
        "relationships": {
            "servers": {
                "data": [
                    {
                        "id": "server1",
                        "type": "servers"
                    },
                    {
                        "id": "server2",
                        "type": "servers"
                    }
                ],
                "links": {
                    "related": "http://localhost:8989/v1/servers/",
                    "self": "http://localhost:8989/v1/monitors/MariaDB-Monitor/relationships/servers/"
                }
            },
            "services": {
                "data": [
                    {
                        "id": "RW-Split-Router",
                        "type": "services"
                    }
                ],
                "links": {
                    "related": "http://localhost:8989/v1/services/",
                    "self": "http://localhost:8989/v1/monitors/MariaDB-Monitor/relationships/services/"
                }
            }
        },
        "type": "monitors"
    },
    "links": {
        "self": "http://localhost:8989/v1/monitors/MariaDB-Monitor/"
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
    "data": [
        {
            "attributes": {
                "module": "mariadbmon",
                "monitor_diagnostics": {
                    "master": "server1",
                    "master_gtid_domain_id": 0,
                    "primary": null,
                    "server_info": [
                        {
                            "gtid_binlog_pos": "0-3000-9",
                            "gtid_current_pos": "0-3000-9",
                            "lock_held": null,
                            "master_group": null,
                            "name": "server1",
                            "read_only": false,
                            "server_id": 3000,
                            "slave_connections": []
                        },
                        {
                            "gtid_binlog_pos": "0-3000-9",
                            "gtid_current_pos": "0-3000-9",
                            "lock_held": null,
                            "master_group": null,
                            "name": "server2",
                            "read_only": false,
                            "server_id": 3001,
                            "slave_connections": [
                                {
                                    "connection_name": "",
                                    "gtid_io_pos": "",
                                    "last_io_error": "",
                                    "last_sql_error": "",
                                    "master_host": "127.0.0.1",
                                    "master_port": 3000,
                                    "master_server_id": 3000,
                                    "seconds_behind_master": 0,
                                    "slave_io_running": "Yes",
                                    "slave_sql_running": "Yes"
                                }
                            ]
                        }
                    ],
                    "state": "Idle"
                },
                "parameters": {
                    "assume_unique_hostnames": true,
                    "auto_failover": false,
                    "auto_rejoin": false,
                    "backend_connect_attempts": 1,
                    "backend_connect_timeout": 3,
                    "backend_read_timeout": 3,
                    "backend_write_timeout": 3,
                    "cooperative_monitoring_locks": "none",
                    "demotion_sql_file": null,
                    "detect_replication_lag": false,
                    "detect_stale_master": null,
                    "detect_stale_slave": null,
                    "detect_standalone_master": null,
                    "disk_space_check_interval": 0,
                    "disk_space_threshold": null,
                    "enforce_read_only_slaves": false,
                    "enforce_simple_topology": false,
                    "enforce_writable_master": false,
                    "events": "all",
                    "failcount": 5,
                    "failover_timeout": 90,
                    "handle_events": true,
                    "ignore_external_masters": false,
                    "journal_max_age": 28800,
                    "maintenance_on_low_disk_space": true,
                    "master_conditions": "primary_monitor_master",
                    "master_failure_timeout": 10,
                    "module": "mariadbmon",
                    "monitor_interval": 5000,
                    "password": "*****",
                    "promotion_sql_file": null,
                    "replication_master_ssl": false,
                    "replication_password": null,
                    "replication_user": null,
                    "script": null,
                    "script_max_replication_lag": -1,
                    "script_timeout": 90,
                    "servers_no_promotion": null,
                    "slave_conditions": "none",
                    "switchover_on_low_disk_space": false,
                    "switchover_timeout": 90,
                    "user": "maxuser",
                    "verify_master_failure": true
                },
                "state": "Running",
                "ticks": 3
            },
            "id": "MariaDB-Monitor",
            "links": {
                "self": "http://localhost:8989/v1/monitors/MariaDB-Monitor/"
            },
            "relationships": {
                "servers": {
                    "data": [
                        {
                            "id": "server1",
                            "type": "servers"
                        },
                        {
                            "id": "server2",
                            "type": "servers"
                        }
                    ],
                    "links": {
                        "related": "http://localhost:8989/v1/servers/",
                        "self": "http://localhost:8989/v1/monitors/MariaDB-Monitor/relationships/servers/"
                    }
                },
                "services": {
                    "data": [
                        {
                            "id": "RW-Split-Router",
                            "type": "services"
                        }
                    ],
                    "links": {
                        "related": "http://localhost:8989/v1/services/",
                        "self": "http://localhost:8989/v1/monitors/MariaDB-Monitor/relationships/services/"
                    }
                }
            },
            "type": "monitors"
        }
    ],
    "links": {
        "self": "http://localhost:8989/v1/monitors/"
    }
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

The request body must be a valid JSON document representing the modified
monitor.

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
DELETE /v1/monitors/:name
```

Destroy a created monitor. The monitor must not have relationships to any
servers in order to be destroyed.

This endpoint also supports the `force=yes` parameter that will unconditionally
delete the monitor by first unlinking it from all servers that it uses.

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
