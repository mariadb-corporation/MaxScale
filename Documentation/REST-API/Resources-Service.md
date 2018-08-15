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

### Create a service

```
POST /v1/services
```

Create a new service by defining the resource. The posted object must define at
least the following fields.

* `data.id`
  * Name of the service

* `data.type`
  * Type of the object, must be `services`

* `data.atttributes.router`
  * The router module to use

* `data.atttributes.parameters.user`
  * The [`user`](../Getting-Started/Configuration-Guide.md#password) to use

* `data.atttributes.parameters.password`
  * The [`password`](../Getting-Started/Configuration-Guide.md#password) to use

The `data.attributes.parameters` object is used to define router and service
parameters. All configuration parameters that can be defined in the
configuration file can also be added to the parameters object. The exceptions to
this are the `type`, `router`, `servers` and `filters` parameters which must not
be defined.

As with other REST API resources, the `data.relationships` field defines the
relationships of the service to other resources. Services can have two types of
relationships: `servers` and `filters` relationships.

If the request body defines a valid `relationships` object, the service is
linked to those resources. For servers, this is equivalent to adding the list of
server names into the
[`servers`](../Getting-Started/Configuration-Guide.md#servers) parameter. For
filters, this is equivalent to adding the filters in the
`data.relationships.filters.data` array to the
[`filters`](../Getting-Started/Configuration-Guide.md#filters) parameter in the
order they appear.

The following example defines a new service with both a server and a filter
relationship.

```javascript
{
    "data": {
        "id": "my-service",
        "type": "services",
        "attributes": {
            "router": "readwritesplit",
            "parameters": {
                "user": "maxuser",
                "password": "maxpwd"
            }
        },
        "relationships": {
            "filters": {
                "data": [
                    {
                        "id": "QLA",
                        "type": "filters"
                    }
                ]
            },
            "servers": {
                "data": [
                    {
                        "id": "server1",
                        "type": "servers"
                    }
                ]
            }
        }
    }
}
```

#### Response

Service is created:

`Status: 204 No Content`

### Destroy a service

```
DELETE /v1/services/:service
```

In the URI , the _:service_ must map to a service that is destroyed.

A service can only be destroyed if the service uses no servers or filters and
all the listeners pointing to the service have been destroyed. This means that
the `data.relationships` must be an empty object and `data.attributes.listeners`
must be an empty array in order for the service to qualify for destruction.

Once a service is destroyed, any listeners associated with it will be freed
after which the ports can be reused by other listeners.

If there are open client connections that use the service when it is destroyed,
they are allowed to gracefully close before the service is destroyed. This means
that the destruction of a service can be acknowledged via the REST API before
the destruction process has fully completed.

To find out whether a service is still in use after it has been destroyed, the
[`sessions`](./Resources-Session.md) resource should be used. If a session for
the service is still open, it has not yet been destroyed.

#### Response

Service is destroyed:

`Status: 204 No Content`

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


#### Response

Listener is created:

`Status: 204 No Content`

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

All standard service parameters can be modified. Refer to the
[service](../Getting-Started/Configuration-Guide.md#service) documentation on
the details of these parameters.

In addition to the standard service parameters, router parameters can be updated
at runtime if the router module supports it. Refer to the individual router
documentation for more details on whether the router supports it and which
parameters can be updated at runtime.

#### Response

Service is modified:

`Status: 204 No Content`

### Update service relationships

```
PATCH /v1/services/:name/relationships/servers
PATCH /v1/services/:name/relationships/filters
```

The _:name_ in the URI must map to a service name with all whitespace replaced
with hyphens.

The request body must be a JSON object that defines only the _data_ field. The
value of the _data_ field must be an array of relationship objects that define
the _id_ and _type_ fields of the relationship. This object will replace the
existing relationships of this type for the service. Both `servers` and
`filters` relationships can be modified.

*Note:* The order of the values in the `filters` relationship will define the
 order the filters are set up in. The order in which the filters appear in the
 array will be the order in which the filters are applied to each query. Refer
 to the [`filters`](../Getting-Started/Configuration-Guide.md#filters) parameter
 for more details.

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

All relationships for a service can be deleted by sending an empty array or a
`null` value as the _data_ field value. The following example removes all
servers from a service.

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
