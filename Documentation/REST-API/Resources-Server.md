# Server Resource

A server resource represents a backend database server.

## Resource Operations

The _:name_ in all of the URIs must be the name of a server in MaxScale.

### Get a server

```
GET /v1/servers/:name
```

Get a single server.

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/servers/server1"
    },
    "data": {
        "id": "server1",
        "type": "servers",
        "attributes": {
            "parameters": {
                "address": "127.0.0.1",
                "disk_space_threshold": null,
                "extra_port": 0,
                "monitorpw": null,
                "monitoruser": null,
                "persistmaxtime": 0,
                "persistpoolmax": 0,
                "port": 3000,
                "priority": 0,
                "proxy_protocol": false,
                "rank": "primary",
                "socket": null,
                "ssl": false,
                "ssl_ca_cert": null,
                "ssl_cert": null,
                "ssl_cert_verify_depth": 9,
                "ssl_key": null,
                "ssl_verify_peer_certificate": false,
                "ssl_verify_peer_host": false,
                "ssl_version": "MAX"
            },
            "state": "Master, Running",
            "version_string": "10.3.22-MariaDB-1:10.3.22+maria~bionic-log",
            "replication_lag": 0,
            "statistics": {
                "connections": 0,
                "total_connections": 0,
                "max_connections": 0,
                "active_operations": 0,
                "routed_packets": 0,
                "persistent_connections": 0,
                "adaptive_avg_select_time": "0ns"
            },
            "node_id": 3000,
            "master_id": -1,
            "last_event": "master_up",
            "triggered_at": "Thu, 09 Apr 2020 07:27:16 GMT",
            "name": "server1",
            "server_id": 3000,
            "read_only": false,
            "gtid_current_pos": "0-3000-24",
            "gtid_binlog_pos": "0-3000-24",
            "master_group": null,
            "lock_held": null,
            "slave_connections": []
        },
        "links": {
            "self": "http://localhost:8989/v1/servers/server1"
        },
        "relationships": {
            "services": {
                "links": {
                    "self": "http://localhost:8989/v1/services/"
                },
                "data": [
                    {
                        "id": "RW-Split-Router",
                        "type": "services"
                    },
                    {
                        "id": "Read-Connection-Router",
                        "type": "services"
                    }
                ]
            },
            "monitors": {
                "links": {
                    "self": "http://localhost:8989/v1/monitors/"
                },
                "data": [
                    {
                        "id": "MariaDB-Monitor",
                        "type": "monitors"
                    }
                ]
            }
        }
    }
}
```

### Get all servers

```
GET /v1/servers
```

#### Response

Response contains a resource collection with all servers.

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/servers/"
    },
    "data": [
        {
            "id": "server1",
            "type": "servers",
            "attributes": {
                "parameters": {
                    "address": "127.0.0.1",
                    "disk_space_threshold": null,
                    "extra_port": 0,
                    "monitorpw": null,
                    "monitoruser": null,
                    "persistmaxtime": 0,
                    "persistpoolmax": 0,
                    "port": 3000,
                    "priority": 0,
                    "proxy_protocol": false,
                    "rank": "primary",
                    "socket": null,
                    "ssl": false,
                    "ssl_ca_cert": null,
                    "ssl_cert": null,
                    "ssl_cert_verify_depth": 9,
                    "ssl_key": null,
                    "ssl_verify_peer_certificate": false,
                    "ssl_verify_peer_host": false,
                    "ssl_version": "MAX"
                },
                "state": "Master, Running",
                "version_string": "10.3.22-MariaDB-1:10.3.22+maria~bionic-log",
                "replication_lag": 0,
                "statistics": {
                    "connections": 0,
                    "total_connections": 0,
                    "max_connections": 0,
                    "active_operations": 0,
                    "routed_packets": 0,
                    "persistent_connections": 0,
                    "adaptive_avg_select_time": "0ns"
                },
                "node_id": 3000,
                "master_id": -1,
                "last_event": "master_up",
                "triggered_at": "Thu, 09 Apr 2020 07:27:16 GMT",
                "name": "server1",
                "server_id": 3000,
                "read_only": false,
                "gtid_current_pos": "0-3000-24",
                "gtid_binlog_pos": "0-3000-24",
                "master_group": null,
                "lock_held": null,
                "slave_connections": []
            },
            "links": {
                "self": "http://localhost:8989/v1/servers/server1"
            },
            "relationships": {
                "services": {
                    "links": {
                        "self": "http://localhost:8989/v1/services/"
                    },
                    "data": [
                        {
                            "id": "RW-Split-Router",
                            "type": "services"
                        },
                        {
                            "id": "Read-Connection-Router",
                            "type": "services"
                        }
                    ]
                },
                "monitors": {
                    "links": {
                        "self": "http://localhost:8989/v1/monitors/"
                    },
                    "data": [
                        {
                            "id": "MariaDB-Monitor",
                            "type": "monitors"
                        }
                    ]
                }
            }
        },
        {
            "id": "server2",
            "type": "servers",
            "attributes": {
                "parameters": {
                    "address": "127.0.0.1",
                    "disk_space_threshold": null,
                    "extra_port": 0,
                    "monitorpw": null,
                    "monitoruser": null,
                    "persistmaxtime": 0,
                    "persistpoolmax": 0,
                    "port": 3001,
                    "priority": 0,
                    "proxy_protocol": false,
                    "rank": "primary",
                    "socket": null,
                    "ssl": false,
                    "ssl_ca_cert": null,
                    "ssl_cert": null,
                    "ssl_cert_verify_depth": 9,
                    "ssl_key": null,
                    "ssl_verify_peer_certificate": false,
                    "ssl_verify_peer_host": false,
                    "ssl_version": "MAX"
                },
                "state": "Slave, Running",
                "version_string": "10.3.22-MariaDB-1:10.3.22+maria~bionic-log",
                "replication_lag": 0,
                "statistics": {
                    "connections": 0,
                    "total_connections": 0,
                    "max_connections": 0,
                    "active_operations": 0,
                    "routed_packets": 0,
                    "persistent_connections": 0,
                    "adaptive_avg_select_time": "0ns"
                },
                "node_id": 3001,
                "master_id": 3000,
                "last_event": "slave_up",
                "triggered_at": "Thu, 09 Apr 2020 07:27:16 GMT",
                "name": "server2",
                "server_id": 3001,
                "read_only": false,
                "gtid_current_pos": "0-3000-24",
                "gtid_binlog_pos": "0-3000-24",
                "master_group": null,
                "lock_held": null,
                "slave_connections": [
                    {
                        "connection_name": "",
                        "master_host": "127.0.0.1",
                        "master_port": 3000,
                        "slave_io_running": "Yes",
                        "slave_sql_running": "Yes",
                        "seconds_behind_master": 0,
                        "master_server_id": 3000,
                        "last_io_error": "",
                        "last_sql_error": "",
                        "gtid_io_pos": ""
                    }
                ]
            },
            "links": {
                "self": "http://localhost:8989/v1/servers/server2"
            },
            "relationships": {
                "services": {
                    "links": {
                        "self": "http://localhost:8989/v1/services/"
                    },
                    "data": [
                        {
                            "id": "RW-Split-Router",
                            "type": "services"
                        }
                    ]
                },
                "monitors": {
                    "links": {
                        "self": "http://localhost:8989/v1/monitors/"
                    },
                    "data": [
                        {
                            "id": "MariaDB-Monitor",
                            "type": "monitors"
                        }
                    ]
                }
            }
        }
    ]
}
```

### Create a server

```
POST /v1/servers
```

Create a new server by defining the resource. The posted object must define at
least the following fields.

* `data.id`
  * Name of the server

* `data.type`
  * Type of the object, must be `servers`

* `data.attributes.parameters.address` OR `data.attributes.parameters.socket`
  * The [`address`](../Getting-Started/Configuration-Guide.md#address) or
    [`socket`](../Getting-Started/Configuration-Guide.md#socket) to use. Only
    one of the fields can be defined.

* `data.attributes.parameters.port`
  * The [`port`](../Getting-Started/Configuration-Guide.md#port) to use. Needs
    to be defined if the `address` field is defined.

The following is the minimal required JSON object for defining a new server.

```javascript
{
    "data": {
        "id": "server3",
        "type": "servers",
        "attributes": {
            "parameters": {
                "address": "127.0.0.1",
                "port": 3003
            }
        }
    }
}
```

The relationships of a server can also be defined at creation time. This allows
new servers to be created and immediately taken into use.

```javascript
{
    "data": {
        "id": "server4",
        "type": "servers",
        "attributes": {
            "parameters": {
                "address": "127.0.0.1",
                "port": 3002
            }
        },
        "relationships": {
            "services": {
                "data": [
                    {
                        "id": "RW-Split-Router",
                        "type": "services"
                    },
                    {
                        "id": "Read-Connection-Router",
                        "type": "services"
                    }
                ]
            },
            "monitors": {
                "data": [
                    {
                        "id": "MySQL-Monitor",
                        "type": "monitors"
                    }
                ]
            }
        }
    }
}
```

Refer to the [Configuration Guide](../Getting-Started/Configuration-Guide.md)
for a full list of server parameters.

#### Response

Server created:

`Status: 204 No Content`

Invalid JSON body:

`Status: 403 Forbidden`

### Update a server

```
PATCH /v1/servers/:name
```

The request body must be a valid JSON document representing the modified
server.

### Modifiable Fields

The following standard server parameters can be modified.

- [address](../Getting-Started/Configuration-Guide.md#address)
- [port](../Getting-Started/Configuration-Guide.md#port)
- [monitoruser](../Getting-Started/Configuration-Guide.md#monitoruser)
- [monitorpw](../Getting-Started/Configuration-Guide.md#monitorpw)

Refer to the documentation on these parameters for valid values.

In addition to standard parameters, the _services_ and _monitors_ fields of the
_relationships_ object can be modified. Removal, addition and modification of
the links will change which service and monitors use this server.

For example, removing the first value in the _services_ list in the
_relationships_ object from the following JSON document will remove the
_server1_ from the service _RW-Split-Router_.

Removing a service from a server is analogous to removing the server from the
service. Both unlink the two objects from each other.

Request for `PATCH /v1/servers/server1` that modifies the address of the server:

```javascript
{
    "data": {
        "attributes": {
            "parameters": {
                "address": "192.168.0.123"
            }
        }
    }
}
```

Request for `PATCH /v1/servers/server1` that modifies the server relationships:

```javascript
{
    "data": {
        "relationships": {
            "services": {
                "data": [
                    { "id": "Read-Connection-Router", "type": "services" }
                ]
            },
            "monitors": {
                "data": [
                    { "id": "MySQL-Monitor", "type": "monitors" }
                ]
            }
        }
    }
}
```

If parts of the resource are not defined (e.g. the `attributes` field in the
above example), those parts of the resource are not modified. All parts that are
defined are interpreted as the new definition of those part of the resource. In
the above example, the `relationships` of the resource are completely redefined.

#### Response

Server modified:

`Status: 204 No Content`

Invalid JSON body:

`Status: 403 Forbidden`

### Update server relationships

```
PATCH /v1/servers/:name/relationships/:type
```

The _:type_ in the URI must be either _services_, for service
relationships, or _monitors_, for monitor relationships.

The request body must be a JSON object that defines only the _data_ field. The
value of the _data_ field must be an array of relationship objects that define
the _id_ and _type_ fields of the relationship. This object will replace the
existing relationships of the particular type from the server.

The following is an example request and request body that defines a single
service relationship for a server.

```
PATCH /v1/servers/my-db-server/relationships/services

{
    data: [
          { "id": "my-rwsplit-service", "type": "services" }
    ]
}
```

All relationships for a server can be deleted by sending an empty array as the
_data_ field value. The following example removes the server from all services.

```
PATCH /v1/servers/my-db-server/relationships/services

{
    data: []
}
```

#### Response

Server relationships modified:

`Status: 204 No Content`

Invalid JSON body:

`Status: 403 Forbidden`

### Destroy a server

```
DELETE /v1/servers/:name
```

A server can only be deleted if it is not used by any services or
monitors.

This endpoint also supports the `force=yes` parameter that will unconditionally
delete the server by first unlinking it from all services and monitors that use
it.

#### Response

Server is destroyed:

`Status: 204 No Content`

Server is in use:

`Status: 403 Forbidden`

### Set server state

```
PUT /v1/servers/:name/set
```

This endpoint requires that the `state` parameter is passed with the
request. The value of `state` must be one of the following values.

|Value      | State Description                |
|-----------|----------------------------------|
|master     | Server is a Master               |
|slave      | Server is a Slave                |
|maintenance| Server is put into maintenance   |
|running    | Server is up and running         |
|synced     | Server is a Galera node          |
|drain      | Server is drained of connections |

For example, to set the server _db-server-1_ into maintenance mode, a request to
the following URL must be made:

```
PUT /v1/servers/db-server-1/set?state=maintenance
```

This endpoint also supports the `force=yes` parameter that will cause all
connections to the server to be closed if `state=maintenance` is also set. By
default setting a server into maintenance mode will cause connections to be
closed only after the next request is sent.

The following example forcefully closes all connections to server _db-server-1_
and sets it into maintenance mode:

```
PUT /v1/servers/db-server-1/set?state=maintenance&force=yes
```

#### Response

Server state modified:

`Status: 204 No Content`

Missing or invalid parameter:

`Status: 403 Forbidden`

### Clear server state

```
PUT /v1/servers/:name/clear
```

This endpoint requires that the `state` parameter is passed with the
request. The value of `state` must be one of the values defined in the
_set_ endpoint documentation.

#### Response

Server state modified:

`Status: 204 No Content`

Missing or invalid parameter:

`Status: 403 Forbidden`

## Server SQL Connection Interface

The following endpoints provide a simple REST API interface for SQL connections
to servers in MaxScale.

The endpoints use JSON Web Tokens to uniquely identify open SQL connections. A
connection token can be acquired with the `/v1/servers/:name/connect` endpoint
and can be used with the `/v1/servers/:name/query`,
`/v1/servers/:name/results/:query_id` and `/v1/servers/:name/disconnect`
endpoints. All of these endpoints accept a connection token in the `token`
parameter of the request:

```
POST /v1/servers/:name/query?token=eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJhZG1pbiIsImV4cCI6MTU4MzI1NDE1MSwiaWF0IjoxNTgzMjI1MzUxLCJpc3MiOiJtYXhzY2FsZSJ9.B1BqhjjKaCWKe3gVXLszpOPfeu8cLiwSb4CMIJAoyqw
```

In addition to request parameters, the token can be stored in cookies in which
case they are automatically used by the REST API. For more information refer to
the documentation for the `/v1/servers/:name/connect` endpoint.

### Open SQL connection to server

```
POST /v1/servers/:name/connect
```

The request body must be a JSON object consisting of the following fields:

- `user`

  - The username to use when connecting to the database.

- `password`

  - The password for the user.

- `db`

  - The default database for the connection, optional. By default the connection
    will have no default database.

- `timeout`

  - Connection timeout in seconds, optional. The default connection timeout is
    10 seconds.

Here is an example request body:

```
{
    "user": "jdoe",
    "password": "my-s3cret",
    "db": "test",
    "timeout": 15
}
```

The response will be a token that represents the connection:

```
{
    "meta": {
        "token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJhZG1pbiIsImV4cCI6MTU4MzI1NDE1MSwiaWF0IjoxNTgzMjI1MzUxLCJpc3MiOiJtYXhzY2FsZSJ9.B1BqhjjKaCWKe3gVXLszpOPfeu8cLiwSb4CMIJAoyqw"
    }
}
```

If the request uses the `persist=yes` request parameter, the token is stored in
cookies and the request body will be empty:

```
POST /v1/servers/:name/connect?persist=yes
```

The token must be given to all subsequent requests that use the connection. It
must be either given in the `token` parameter of a request or it must be stored
in the cookies. If both a `token` parameter and a cookie exist at the same time,
the `token` parameter will be used instead of the cookie.

If this endpoint is called with a valid token in the `token` parameter or a
token stored in the cookies, the existing connection will be closed before a new
one is opened. This is done to prevent leakage of connections when using cookies
as the token storage method.

#### Response

Connection was opened:

`Status: 200 OK`

Connection was opened with `persist=yes`:

`Status: 204 No Content`

Missing or invalid payload:

`Status: 403 Forbidden`

### Close an opened SQL connection

```
POST /v1/servers/:name/disconnect
```

#### Response

Connection was closed:

`Status: 204 No Content`

Missing connection token:

`Status: 403 Forbidden`

### Execute SQL query

```
POST /v1/servers/:name/queries
```

The request body must be a JSON object with the value of the `sql` field set to
the SQL to be executed:

```
{
    "sql": "SELECT * FROM test.t1"
}
```

The response body will contain the result of the query:

```
{
    "data": {
        "attributes": {
            "results": [
                {
                    "data": [
                        [
                            1
                        ]
                    ],
                    "fields": [
                        "id"
                    ]
                }
            ]
        },
        "id": "1-1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/servers/server1/queries/1-1/"
    }
}
```

By default, the complete result is returned in the response body. If the SQL
query returns more than one result, the `results` array will contain all the
results.

The `results` array can have three types of objects: resultsets, errors, and OK
responses.

- A resultset consists of the `data` field with the result data stored as a two
  dimensional array. The names of the fields are stored in an array in the
  `fields` field. These types of results will be returned for any operation that
  returns rows (i.e. `SELECT` statements)

- An error consists of an object with the `errno` field set to the MariaDB error
  code, the `message` field set to the human-readable error message and the
  `sqlstate` field set to the current SQLSTATE of the connection.

- An OK response is returned for any result that completes successfully but not
  return rows (e.g. an `INSERT` or `UPDATE` statement). The `affected_rows`
  field contains the number of rows affected by the operation, the
  `last_insert_id` contains the auto-generated ID and the `warnings` field
  contains the number of warnings raised by the operation.

This endpoint supports the `page[size]` parameter which limits the number of
rows read from the resultset. The page size must be a positive number. If the
number of rows exceeds the given limit, the rest of the result is available at
the URL stored in `data.links.next`. If this link does not exist, the result was
smaller than the given limit.

#### Response

Query successfully executed:

`Status: 201 Created`

Invalid payload or missing connection token:

`Status: 403 Forbidden`

### Read result of SQL query

```
GET /v1/servers/:name/queries/:query_id
```

If the query result was not fully returned in the initial POST request, the
remaining result will be available at this endpoint. The format of the data is
identical to the POST version of this endpoint. This endpoint accepts the same
result pagination parameter `page[size]` that the POST version does.

Note that each query result can only be read once. Subsequent attempts to read a
result of a query will be answered with a HTTP 404 Not Found.

#### Response

Query result read:

`Status: 200 OK`

Missing connection token:

`Status: 403 Forbidden`

Result already read:

`Status: 404 Not Found`
