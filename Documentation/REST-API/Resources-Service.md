# Service Resource

A service resource represents a service inside MaxScale. A service is a
collection of network listeners, filters, a router and a set of backend servers.

## Resource Operations

### Get a service

Get a single service. The _:name_ in the URI must be a valid service name with
all whitespace replaced with hyphens. The service names are case-insensitive.

```
GET /v1/services/:name
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/services/Read-Connection-Router"
    },
    "data": {
        "id": "Read-Connection-Router",
        "type": "services",
        "attributes": {
            "router": "readconnroute",
            "state": "Started",
            "router_diagnostics": {
                "connections": 0,
                "current_connections": 1,
                "queries": 0
            },
            "started": "Mon May 22 12:54:05 2017",
            "total_connections": 1,
            "connections": 1,
            "parameters": { // Service parameters
                "router_options": "master",
                "user": "maxuser",
                "password": "maxpwd",
                "enable_root_user": false,
                "max_retry_interval": 3600,
                "max_connections": 0,
                "connection_timeout": 0,
                "auth_all_servers": false,
                "strip_db_esc": true,
                "localhost_match_wildcard_host": true,
                "version_string": "",
                "log_auth_warnings": true,
                "retry_on_failure": true
            },
            "listeners": [ // Listeners that point to this service
                {
                    "attributes": {
                        "parameters": {
                            "port": 4008,
                            "protocol": "MySQLClient",
                            "authenticator": "MySQLAuth"
                        }
                    },
                    "id": "Read-Connection-Listener",
                    "type": "listeners"
                }
            ]
        },
        "relationships": {
            "servers": {
                "links": {
                    "self": "http://localhost:8989/v1/servers/"
                },
                "data": [ // List of servers that this service uses
                    {
                        "id": "server1",
                        "type": "servers"
                    }
                ]
            }
        },
        "links": {
            "self": "http://localhost:8989/v1/services/Read-Connection-Router"
        }
    }
}
```

### Get all services

Get all services.

```
GET /v1/services
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/services/"
    },
    "data": [ // Collection of service resources
        {
            "id": "Read-Connection-Router",
            "type": "services",
            "attributes": {
                "router": "readconnroute",
                "state": "Started",
                "router_diagnostics": {
                    "connections": 0,
                    "current_connections": 1,
                    "queries": 0
                },
                "started": "Mon May 22 13:00:46 2017",
                "total_connections": 1,
                "connections": 1,
                "parameters": {
                    "router_options": "master",
                    "user": "maxuser",
                    "password": "maxpwd",
                    "enable_root_user": false,
                    "max_retry_interval": 3600,
                    "max_connections": 0,
                    "connection_timeout": 0,
                    "auth_all_servers": false,
                    "strip_db_esc": true,
                    "localhost_match_wildcard_host": true,
                    "version_string": "",
                    "log_auth_warnings": true,
                    "retry_on_failure": true
                },
                "listeners": [
                    {
                        "attributes": {
                            "parameters": {
                                "port": 4008,
                                "protocol": "MySQLClient",
                                "authenticator": "MySQLAuth"
                            }
                        },
                        "id": "Read-Connection-Listener",
                        "type": "listeners"
                    }
                ]
            },
            "relationships": {
                "servers": {
                    "links": {
                        "self": "http://localhost:8989/v1/servers/"
                    },
                    "data": [
                        {
                            "id": "server1",
                            "type": "servers"
                        }
                    ]
                }
            },
            "links": {
                "self": "http://localhost:8989/v1/services/Read-Connection-Router"
            }
        },
        {
            "id": "CLI",
            "type": "services",
            "attributes": {
                "router": "cli",
                "state": "Started",
                "started": "Mon May 22 13:00:46 2017",
                "total_connections": 2,
                "connections": 2,
                "parameters": {
                    "router_options": "",
                    "user": "",
                    "password": "",
                    "enable_root_user": false,
                    "max_retry_interval": 3600,
                    "max_connections": 0,
                    "connection_timeout": 0,
                    "auth_all_servers": false,
                    "strip_db_esc": true,
                    "localhost_match_wildcard_host": true,
                    "version_string": "",
                    "log_auth_warnings": true,
                    "retry_on_failure": true
                },
                "listeners": [
                    {
                        "attributes": {
                            "parameters": {
                                "address": "default",
                                "port": 0,
                                "protocol": "maxscaled",
                                "authenticator": "MaxAdminAuth"
                            }
                        },
                        "id": "CLI-Listener",
                        "type": "listeners"
                    },
                    {
                        "attributes": {
                            "parameters": {
                                "address": "0.0.0.0",
                                "port": 6603,
                                "protocol": "maxscaled",
                                "authenticator": "MaxAdminAuth"
                            }
                        },
                        "id": "CLI-Network-Listener",
                        "type": "listeners"
                    }
                ]
            },
            "relationships": {},
            "links": {
                "self": "http://localhost:8989/v1/services/CLI"
            }
        }
    ]
}
```

### Get service listeners

Get the listeners of a service. The _:name_ in the URI must be a valid service
name with all whitespace replaced with hyphens.

```
GET /v1/services/:name/listeners
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/services/Read-Connection-Router/listeners"
    },
    "data": [
        {
            "attributes": {
                "parameters": {
                    "port": 4008,
                    "protocol": "MySQLClient",
                    "authenticator": "MySQLAuth"
                }
            },
            "id": "Read-Connection-Listener",
            "type": "listeners"
        }
    ]
}
```

### Get service listeners

Get the listeners of a service. The _:name_ in the URI must be a valid service
name with all whitespace replaced with hyphens.

```
GET /v1/services/:name/listeners
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/services/Read-Connection-Router/listeners"
    },
    "data": [
        {
            "attributes": {
                "parameters": {
                    "port": 4008,
                    "protocol": "MySQLClient",
                    "authenticator": "MySQLAuth"
                }
            },
            "id": "Read-Connection-Listener",
            "type": "listeners"
        }
    ]
}
```

### Get a sigle service listener

Get the listeners of a service. The _:name_ in the URI must be a valid service
name and _:listener_ must be a valid listener name, both with all whitespace
replaced with hyphens.

```
GET /v1/services/:name/listeners/:listener
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/services/RW-Split-Router/listeners/RW-Split-Listener"
    },
    "data": {
        "attributes": {
            "parameters": {
                "port": 4006,
                "protocol": "MySQLClient",
                "authenticator": "MySQLAuth"
            }
        },
        "id": "RW-Split-Listener",
        "type": "listeners"
    }
}
```

### Create a new listener


```
POST /v1/services/:name/listeners
```

Create a new listener for a service by defining the resource. The _:name_ in the
URI must map to a service name with all whitespace replaced with hyphens. The
posted object must define the _data.id_ field with the name of the server and
the _data.attributes.parameters.port_ field with the port where the listener
will listen on. The following is the minimal required JSON object for defining a
new listener.

```javascript
{
    "data": {
        "id": "my-listener",
        "type": "listeners",
        "attributes": {
            "parameters": {
                "port": 3306
            }
        }
    }
}
```

The following table contains all values that can be given in the _parameters_
object.

|Parameter              | Description                                       |
|-----------------------|---------------------------------------------------|
|port                   | Port to listen on                                 |
|address                | Interface to listen on                            |
|authenticator          | Authenticator module                              |
|authenticator_options  | Options for the authenticator                     |
|ssl_key                | Path to SSL key                                   |
|ssl_cert               | Path to SSL certificate                           |
|ssl_ca_cert            | Path to SSL CA certificate                        |
|ssl_version            | The SSL version to use [TLSv1.2|TLSv1.1|TLSv1.0]  |
|ssl_cert_verify_depth  | Certificate verification depth                    |

### Destroy a listener

```
DELETE /v1/services/:service/listeners/:name
```

In the URI , the _:name_ must map to a listener and the _:service_ must map to a
service. Both names must have all whitespace replaced with hyphens.

#### Response

OK:

```
Status: 204 No Content
```

Listener not found:

```
Status: 404 Not Found
```

Listener cannot be deleted:

```
Status: 403 Forbidden
```

### Update a service

The _:name_ in the URI must map to a service name and the request body must be a
valid JSON Patch document which is applied to the resource.

```
PATCH /v1/services/:name
```

The following standard service parameters can be modified.

- [user](../Getting-Started/Configuration-Guide.md#user)
- [password](../Getting-Started/Configuration-Guide.md#password)
- [enable_root_user](../Getting-Started/Configuration-Guide.md#enable_root_user)
- [max_retry_interval](../Getting-Started/Configuration-Guide.md#max_retry_interval)
- [max_connections](../Getting-Started/Configuration-Guide.md#max_connections)
- [connection_timeout](../Getting-Started/Configuration-Guide.md#connection_timeout)
- [auth_all_servers](../Getting-Started/Configuration-Guide.md#auth_all_servers)
- [strip_db_esc](../Getting-Started/Configuration-Guide.md#strip_db_esc)
- [localhost_match_wildcard_host](../Getting-Started/Configuration-Guide.md#localhost_match_wildcard_host)
- [version_string](../Getting-Started/Configuration-Guide.md#version_string)
- [weightby](../Getting-Started/Configuration-Guide.md#weightby)
- [log_auth_warnings](../Getting-Started/Configuration-Guide.md#log_auth_warnings)
- [retry_on_failure](../Getting-Started/Configuration-Guide.md#retry_on_failure)

Refer to the documentation on these parameters for valid values.

#### Response

Service is modified.

```
Status: 204 No Content
```

### Stop a service

Stops a started service.

```
PUT /v1/service/:name/stop
```

#### Response

Service is stopped.

`Status: 204 No Content`

### Start a service

Starts a stopped service.

```
PUT /v1/service/:name/start
```

#### Response

Service is started.

`Status: 204 No Content`
