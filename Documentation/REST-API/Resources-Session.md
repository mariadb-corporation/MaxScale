# Session Resource

A session is an abstraction of a client connection, any number of related backend
connections, a router module session and possibly filter module sessions. Each
session is created on a service and each service can have multiple sessions.

## Resource Operations

### Get a session

Get a single session. _:id_ must be a valid session ID.

```
GET /v1/sessions/:id
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/sessions/1"
    },
    "data": {
        "id": "1",
        "type": "sessions",
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
        },
        "attributes": {
            "state": "Listener Session",
            "connected": "Wed May 17 10:06:35 2017"
        },
        "links": {
            "self": "http://localhost:8989/v1/sessions/1"
        }
    }
}
```

#### Supported Request Parameter

- `pretty`

### Get all sessions

Get all sessions.

```
GET /v1/sessions
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/sessions/"
    },
    "data": [
        {
            "id": "1",
            "type": "sessions",
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
            },
            "attributes": {
                "state": "Listener Session",
                "connected": "Wed May 17 10:06:35 2017"
            },
            "links": {
                "self": "http://localhost:8989/v1/sessions/1"
            }
        },
        {
            "id": "2",
            "type": "sessions",
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
            },
            "attributes": {
                "state": "Listener Session",
                "connected": "Wed May 17 10:06:35 2017"
            },
            "links": {
                "self": "http://localhost:8989/v1/sessions/2"
            }
        },
        {
            "id": "3",
            "type": "sessions",
            "relationships": {
                "services": {
                    "links": {
                        "self": "http://localhost:8989/v1/services/"
                    },
                    "data": [
                        {
                            "id": "CLI",
                            "type": "services"
                        }
                    ]
                }
            },
            "attributes": {
                "state": "Listener Session",
                "connected": "Wed May 17 10:06:35 2017"
            },
            "links": {
                "self": "http://localhost:8989/v1/sessions/3"
            }
        }
    ]
}
```

#### Supported Request Parameter

- `pretty`
