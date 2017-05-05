# Server Resource

A server resource represents a backend database server.

## Resource Operations

### Get a server

```
GET /servers/:name
```

Get a single server. The _:name_ in the URI must be a valid server name with all
whitespace replaced with hyphens. The server names are case-insensitive.

**Note**: The _parameters_ field contains all custom parameters for
  servers, including the server weighting parameters.

#### Response

```
Status: 200 OK

{
    "name": "server1",
    "parameters": {
        "address": "127.0.0.1",
        "port": 3000,
        "protocol": "MySQLBackend",
        "monitoruser": "maxuser",
        "monitorpw": "maxpwd"
    },
    "status": "Master, Running",
    "version": "10.1.22-MariaDB",
    "node_id": 3000,
    "master_id": -1,
    "replication_depth": 0,
    "slaves": [
        3001
    ],
    "statictics": {
        "connections": 0,
        "total_connections": 0,
        "active_operations": 0
    },
    "relationships": {
        "self": "http://localhost:8989/servers/server1",
        "services": [
            "http://localhost:8989/services/RW-Split-Router",
            "http://localhost:8989/services/Read-Connection-Router"
        ],
        "monitors": [
            "http://localhost:8989/monitors/MySQL-Monitor"
        ]
    }
}
```

Server not found:

```
Status: 404 Not Found
```

#### Supported Request Parameter

- `pretty`

### Get all servers

```
GET /servers
```

#### Response

Response contains an array of all servers.

```
Status: 200 OK

[
    {
        "name": "server1",
        "parameters": {
            "address": "127.0.0.1",
            "port": 3000,
            "protocol": "MySQLBackend",
            "monitoruser": "maxuser",
            "monitorpw": "maxpwd"
        },
        "status": "Master, Running",
        "version": "10.1.22-MariaDB",
        "node_id": 3000,
        "master_id": -1,
        "replication_depth": 0,
        "slaves": [
            3001
        ],
        "statictics": {
            "connections": 0,
            "total_connections": 0,
            "active_operations": 0
        },
        "relationships": {
            "self": "http://localhost:8989/servers/server1",
            "services": [
                "http://localhost:8989/services/RW-Split-Router",
                "http://localhost:8989/services/Read-Connection-Router"
            ],
            "monitors": [
                "http://localhost:8989/monitors/MySQL-Monitor"
            ]
        }
    },
    {
        "name": "server2",
        "parameters": {
            "address": "127.0.0.1",
            "port": 3001,
            "protocol": "MySQLBackend",
            "my-weighting-parameter": "3"
        },
        "status": "Slave, Running",
        "version": "10.1.22-MariaDB",
        "node_id": 3001,
        "master_id": 3000,
        "replication_depth": 1,
        "slaves": [],
        "statictics": {
            "connections": 0,
            "total_connections": 0,
            "active_operations": 0
        },
        "relationships": {
            "self": "http://localhost:8989/servers/server2",
            "services": [
                "http://localhost:8989/services/RW-Split-Router"
            ],
            "monitors": [
                "http://localhost:8989/monitors/MySQL-Monitor"
            ]
        }
    }
]
```

#### Supported Request Parameter

- `pretty`

### Create a server

```
POST /servers
```

Create a new server by defining the resource. The posted object must define the
_name_ field with the name of the server and the _parameters_ field with JSON
object containing values for the _address_ and _port_ parameters. The following
is the minimal required JSON object for defining a new server.

```
{
    "name": "test-server",
    "parameters": {
        "address": "127.0.0.1",
        "port": 3003
    }
}
```

#### Response

Response contains the created resource.

```
Status: 200 OK

{
    "name": "test-server",
    "parameters": {
        "address": "127.0.0.1",
        "port": 3003,
        "protocol": "MySQLBackend"
    },
    "status": "Running",
    "node_id": -1,
    "master_id": -1,
    "replication_depth": -1,
    "slaves": [],
    "statictics": {
        "connections": 0,
        "total_connections": 0,
        "active_operations": 0
    },
    "relationships": {
        "self": "http://localhost:8989/servers/test-server"
    }
}
```

Invalid JSON body:

```
Status: 400 Bad Request
```

#### Supported Request Parameter

- `pretty`

### Update a server

```
PUT /servers/:name
```

The _:name_ in the URI must map to a server name with all whitespace replaced
with hyphens and the request body must be a valid JSON document representing the
modified server. If the server in question is not found, a 404 Not Found
response is returned.

### Modifiable Fields

The following standard server parameter can be modified.

- [address](../Getting-Started/Configuration-Guide.md#address)
- [port](../Getting-Started/Configuration-Guide.md#port)
- [monitoruser](../Getting-Started/Configuration-Guide.md#monitoruser)
- [monitorpw](../Getting-Started/Configuration-Guide.md#monitorpw)

Refer to the documentation on these parameters for valid values.

The server weighting parameters can also be added, removed and updated. To
remove a parameter, define the value of that parameter as the _null_ JSON type
e.g.  `{ "my-param": null }`. To add a parameter, add a new key-value pair to
the _parameters_ object with a name that does not conflict with the standard
parameters. To modify a weighting parameter, simply change the value.

In addition to standard parameters, the _services_ and _monitors_ fields of the
_relationships_ object can be modified. Removal, addition and modification of
the links will change which service and monitors use this server.

For example, removing the first value in the _services_ list in the
_relationships_ object from the following JSON document will remove the
_server1_ from the service _RW-Split-Router_.

Removing a service from a server is analogous to removing the server from the
service. Both unlink the two objects from each other.

```
{
    "name": "server1",
    "parameters": {
        "address": "127.0.0.1",
        "port": 3000,
        "protocol": "MySQLBackend",
        "monitoruser": "maxuser",
        "monitorpw": "maxpwd"
    },
    "status": "Master, Running",
    "version": "10.1.22-MariaDB",
    "node_id": 3000,
    "master_id": -1,
    "replication_depth": 0,
    "slaves": [
        3001
    ],
    "statictics": {
        "connections": 0,
        "total_connections": 0,
        "active_operations": 0
    },
    "relationships": {
        "self": "http://localhost:8989/servers/server1",
        "services": [
            "http://localhost:8989/services/RW-Split-Router", // This value is removed
            "http://localhost:8989/services/Read-Connection-Router"
        ],
        "monitors": [
            "http://localhost:8989/monitors/MySQL-Monitor"
        ]
    }
}
```

#### Response

Response contains the modified resource.

```
Status: 200 OK

{
    "name": "server1",
    "parameters": {
        "address": "127.0.0.1",
        "port": 3000,
        "protocol": "MySQLBackend",
        "monitoruser": "maxuser",
        "monitorpw": "maxpwd"
    },
    "status": "Master, Running",
    "version": "10.1.22-MariaDB",
    "node_id": 3000,
    "master_id": -1,
    "replication_depth": 0,
    "slaves": [
        3001
    ],
    "statictics": {
        "connections": 0,
        "total_connections": 0,
        "active_operations": 0
    },
    "relationships": {
        "self": "http://localhost:8989/servers/server1",
        "services": [
            "http://localhost:8989/services/Read-Connection-Router"
        ],
        "monitors": [
            "http://localhost:8989/monitors/MySQL-Monitor"
        ]
    }
}
```

Server not found:

```
Status: 404 Not Found
```

Invalid JSON body:

```
Status: 400 Bad Request
```

#### Supported Request Parameter

- `pretty`

### Destroy a server

```
DELETE /servers/:name
```

The _:name_ in the URI must map to a server name with all whitespace replaced
with hyphens.

A server can only be deleted if the only relations in the _relationships_ object
is the _self_ link.

#### Response

OK:

```
Status: 204 No Content
```

Server not found:

```
Status: 404 Not Found
```

Server is in use:

```
Status: 400 Bad Request
```

# **TODO:** Implement the following features

### Get all connections to a server

Get all connections that are connected to a server.

```
GET /servers/:name/connections
```

#### Response

```
Status: 200 OK

[
    {
        "state": "DCB in the polling loop",
        "role": "Backend Request Handler",
        "server": "/servers/db-serv-01",
        "service": "/services/my-service",
        "statistics": {
            "reads":             2197
            "writes":            1562
            "buffered_writes":   0
            "high_water_events": 0
            "low_water_events":  0
        }
    },
    {
        "state": "DCB in the polling loop",
        "role": "Backend Request Handler",
        "server": "/servers/db-serv-01",
        "service": "/services/my-second-service"
        "statistics": {
            "reads":             0
            "writes":            0
            "buffered_writes":   0
            "high_water_events": 0
            "low_water_events":  0
        }
    }
]
```

#### Supported Request Parameter

- `fields`
- `range`

### Close all connections to a server

Close all connections to a particular server. This will forcefully close all
backend connections.

```
DELETE /servers/:name/connections
```

#### Response

```
Status: 204 No Content
```
