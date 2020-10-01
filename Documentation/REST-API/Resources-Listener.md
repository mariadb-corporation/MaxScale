# Listener Resource

A listener resource represents a listener of a service in MaxScale. All
listeners point to a service in MaxScale.

## Resource Operations

### Get a listener

```
GET /v1/listeners/:name
```

Get a single listener. The _:name_ in the URI must be the name of a listener in
MaxScale.

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/listeners/RW-Split-Listener"
    },
    "data": {
        "id": "RW-Split-Listener",
        "type": "listeners",
        "attributes": {
            "state": "Running",
            "parameters": {
                "protocol": "MariaDBClient",
                "port": 4006,
                "socket": null,
                "authenticator_options": "",
                "address": "::",
                "authenticator": null,
                "ssl": "false",
                "ssl_cert": null,
                "ssl_key": null,
                "ssl_ca_cert": null,
                "ssl_crl": null,
                "ssl_version": "MAX",
                "ssl_cert_verify_depth": 9,
                "ssl_verify_peer_certificate": false,
                "ssl_verify_peer_host": false,
                "sql_mode": null
            }
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
            }
        }
    }
}
```

### Get all listeners

```
GET /v1/listeners
```

Get all listeners.

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/listeners/"
    },
    "data": [
        {
            "id": "Read-Connection-Listener",
            "type": "listeners",
            "attributes": {
                "state": "Running",
                "parameters": {
                    "protocol": "MariaDBClient",
                    "port": 4008,
                    "socket": null,
                    "authenticator_options": "",
                    "address": "::",
                    "authenticator": null,
                    "ssl": "false",
                    "ssl_cert": null,
                    "ssl_key": null,
                    "ssl_ca_cert": null,
                    "ssl_crl": null,
                    "ssl_version": "MAX",
                    "ssl_cert_verify_depth": 9,
                    "ssl_verify_peer_certificate": false,
                    "ssl_verify_peer_host": false,
                    "sql_mode": null
                }
            },
            "relationships": {
                "services": {
                    "links": {
                        "self": "http://localhost:8989/v1/services/"
                    },
                    "data": [
                        {
                            "id": "Read-Connection-Router",
                            "type": "services"
                        }
                    ]
                }
            }
        },
        {
            "id": "RW-Split-Listener",
            "type": "listeners",
            "attributes": {
                "state": "Running",
                "parameters": {
                    "protocol": "MariaDBClient",
                    "port": 4006,
                    "socket": null,
                    "authenticator_options": "",
                    "address": "::",
                    "authenticator": null,
                    "ssl": "false",
                    "ssl_cert": null,
                    "ssl_key": null,
                    "ssl_ca_cert": null,
                    "ssl_crl": null,
                    "ssl_version": "MAX",
                    "ssl_cert_verify_depth": 9,
                    "ssl_verify_peer_certificate": false,
                    "ssl_verify_peer_host": false,
                    "sql_mode": null
                }
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
                }
            }
        }
    ]
}
```

### Create a new listener

```
POST /v1/listeners
```

Creates a new listener. The request body must define the following fields.

* `data.id`
  * Name of the listener

* `data.type`
  * Type of the object, must be `listeners`

* `data.attributes.parameters.port` OR `data.attributes.parameters.socket`
  * The TCP port or UNIX Domain Socket the listener listens on. Only one of the
    fields can be defined.

* `data.relationships.services.data`
  * The service relationships data, must define a JSON object with an `id` value
    that defines the service to use and a `type` value set to `services`.

The following is the minimal required JSON object for defining a new listener.

```javascript
{
    "data": {
        "id": "my-listener",
        "type": "listeners",
        "attributes": {
            "parameters": {
                "port": 3306
            }
        },
        "relationships": {
            "services": {
                "data": [
                    {"id": "RW-Split-Router", "type": "services"}
                ]
            }
        }
    }
}
```

Refer to the [Configuration Guide](../Getting-Started/Configuration-Guide.md) for
a full list of listener parameters.

#### Response

Listener is created:

`Status: 204 No Content`

### Destroy a listener

```
DELETE /v1/listeners/:name
```

The _:name_ must be a valid listener name. When a listener is destroyed, the
network port it listens on is available for reuse.

#### Response

Listener is destroyed:

`Status: 204 No Content`

Listener cannot be deleted:

`Status: 403 Forbidden`

### Stop a listener

```
PUT /v1/listeners/:name/stop
```

Stops a started listener. When a listener is stopped, new connections are no
longer accepted and are queued until the listener is started again.

#### Response

Listener is stopped:

`Status: 204 No Content`

### Start a listener

```
PUT /v1/listeners/:name/start
```

Starts a stopped listener.

#### Response

Listener is started:

`Status: 204 No Content`
