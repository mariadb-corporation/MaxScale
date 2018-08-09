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
                            "protocol": "MariaDBClient",
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
                                "protocol": "MariaDBClient",
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
                    "protocol": "MariaDBClient",
                    "authenticator": "MySQLAuth"
                }
            },
            "id": "Read-Connection-Listener",
            "type": "listeners"
        }
    ]
}
```

### Get a single service listener

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
                "protocol": "MariaDBClient",
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

The following values can be given in the _parameters_ object. If SSL options are
provided, the _ssl_key_, _ssl_cert_ and _ssl_ca_cert_ parameters must all be
defined.

- [address](../Getting-Started/Configuration-Guide.md#user-content-address-1)
- [port](../Getting-Started/Configuration-Guide.md#user-content-port-1)
- [protocol](../Getting-Started/Configuration-Guide.md#user-content-protocol-1)
- [authenticator](../Getting-Started/Configuration-Guide.md#user-content-authenticator-1)
- [authenticator_options](../Getting-Started/Configuration-Guide.md#user-content-authenticator-options-1)
- [ssl_key](../Getting-Started/Configuration-Guide.md#user-content-ssl_key-1)
- [ssl_cert](../Getting-Started/Configuration-Guide.md#user-content-ssl_cert-1)
- [ssl_ca_cert](../Getting-Started/Configuration-Guide.md#user-content-ssl_ca_cert-1)
- [ssl_version](../Getting-Started/Configuration-Guide.md#user-content-ssl_version-1)
- [ssl_cert_verify_depth](../Getting-Started/Configuration-Guide.md#user-content-ssl_cert_verify_depth-1)

### Destroy a listener

```
DELETE /v1/services/:service/listeners/:name
```

In the URI , the _:name_ must map to a listener and the _:service_ must map to a
service. Both names must have all whitespace replaced with hyphens.

#### Response

Listener is destroyed:

`Status: 204 No Content`

Listener cannot be deleted:

`Status: 403 Forbidden`

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

Service is modified:

`Status: 204 No Content`

### Update service relationships

```
PATCH /v1/services/:name/relationships/servers
```

The _:name_ in the URI must map to a service name with all whitespace replaced
with hyphens.

The request body must be a JSON object that defines only the _data_ field. The
value of the _data_ field must be an array of relationship objects that define
the _id_ and _type_ fields of the relationship. This object will replace the
existing relationships of the service.

The following is an example request and request body that defines a single
server relationship for a service.

```
PATCH /v1/services/my-rw-service/relationships/servers

{
    data: [
          { "id": "my-server", "type": "servers" }
    ]
}
```

All relationships for a service can be deleted by sending an empty array as the
_data_ field value. The following example removes all servers from a service.

```
PATCH /v1/services/my-rw-service/relationships/servers

{
    data: []
}
```

#### Response

Service relationships modified:

`Status: 204 No Content`

Invalid JSON body:

`Status: 403 Forbidden`

### Stop a service

Stops a started service.

```
PUT /v1/services/:name/stop
```

#### Response

Service is stopped:

`Status: 204 No Content`

### Start a service

Starts a stopped service.

```
PUT /v1/services/:name/start
```

#### Response

Service is started:

`Status: 204 No Content`
