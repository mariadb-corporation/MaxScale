# Server Resource

A server resource represents a backend database server.

## Resource Operations

### Get a server

Get a single server. The _:name_ in the URI must be a valid server name with all
whitespace replaced with hyphens. The server names are case-insensitive.

```
GET /servers/:name
```

#### Response

```
Status: 200 OK

{
    "name": "db-serv-1",
    "address": "192.168.121.58",
    "port": 3306,
    "protocol": "MySQLBackend",
    "status": [
        "master",
        "running"
    ],
    "parameters": {
        "report_weight": 10,
        "app_weight": 2
    }
}
```

**Note**: The _parameters_ field contains all custom parameters for
  servers, including the server weighting parameters.

#### Supported Request Parameter

- `fields`

### Get all servers

```
GET /servers
```

#### Response

```
Status: 200 OK

[
    {
        "name": "db-serv-1",
        "address": "192.168.121.58",
        "port": 3306,
        "protocol": "MySQLBackend",
        "status": [
            "master",
            "running"
        ],
        "parameters": {
            "report_weight": 10,
            "app_weight": 2
        }
    },
    {
        "name": "db-serv-2",
        "address": "192.168.121.175",
        "port": 3306,
        "status": [
            "slave",
            "running"
        ],
        "protocol": "MySQLBackend",
        "parameters": {
            "app_weight": 6
        }
    }
]
```

#### Supported Request Parameter

- `fields`
- `range`

### Update a server

**Note**: The update mechanisms described here are provisional and most likely
  will change in the future. This description is only for design purposes and
  does not yet work.

Partially update a server. The _:name_ in the URI must map to a server name with
all whitespace replaced with hyphens and the request body must be a valid JSON
Patch document which is applied to the resource.

```
PATCH /servers/:name
```

### Modifiable Fields

|Field      |Type        |Description                                                                  |
|-----------|------------|-----------------------------------------------------------------------------|
|address    |string      |Server address                                                               |
|port       |number      |Server port                                                                  |
|parameters |object      |Server extra parameters                                                      |
|state      |string array|Server state, array of `master`, `slave`, `synced`, `running` or `maintenance`. An empty array is interpreted as a server that is down.|

```
{
    { "op": "replace", "path": "/address", "value": "192.168.0.100" },
    { "op": "replace", "path": "/port", "value": 4006 },
    { "op": "add", "path": "/state/0", "value": "maintenance" },
    { "op": "replace", "path": "/parameters/report_weight", "value": 1 }
}
```

#### Response

Response contains the modified resource.

```
Status: 200 OK

{
    "name": "db-serv-1",
    "protocol": "MySQLBackend",
    "address": "192.168.0.100",
    "port": 4006,
    "state": [
        "maintenance",
        "running"
    ],
    "parameters": {
        "report_weight": 1,
        "app_weight": 2
    }
}
```

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
