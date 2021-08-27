# Session Resource

A session is an abstraction of a client connection, any number of related backend
connections, a router module session and possibly filter module sessions. Each
session is created on a service and each service can have multiple sessions.

[TOC]

## Resource Operations

### Get a session

```
GET /v1/sessions/:id
```

Get a single session. _:id_ must be a valid session ID. The session ID is the
same that is exposed to the client as the connection ID.

This endpoint also supports the `rdns=true` parameter, which instructs MaxScale to
perform reverse DNS on the client IP address. As this requires communicating with
an external server, the operation may be expensive.

#### Response

`Status: 200 OK`

```javascript
{
    "data": {
        "attributes": {
            "client": {
                "cipher": ""
            },
            "connected": "Fri Jul 16 09:51:12 2021",
            "connections": [
                {
                    "cipher": "",
                    "connection_id": 233,
                    "server": "server1"
                },
                {
                    "cipher": "",
                    "connection_id": 134,
                    "server": "server2"
                }
            ],
            "idle": 59.600000000000001,
            "log": [],
            "queries": [],
            "remote": "::ffff:127.0.0.1",
            "state": "Session started",
            "user": "maxuser"
        },
        "id": "1",
        "links": {
            "self": "http://localhost:8989/v1/sessions/1"
        },
        "relationships": {
            "services": {
                "data": [
                    {
                        "id": "RW-Split-Router",
                        "type": "services"
                    }
                ],
                "links": {
                    "related": "http://localhost:8989/v1/services/",
                    "self": "http://localhost:8989/v1/sessions/1/relationships/services"
                }
            }
        },
        "type": "sessions"
    },
    "links": {
        "self": "http://localhost:8989/v1/sessions/1"
    }
}
```

### Get all sessions

```
GET /v1/sessions
```

Get all sessions.

#### Response

`Status: 200 OK`

```javascript
{
    "data": [
        {
            "attributes": {
                "client": {
                    "cipher": ""
                },
                "connected": "Fri Jul 16 09:51:12 2021",
                "connections": [
                    {
                        "cipher": "",
                        "connection_id": 233,
                        "server": "server1"
                    },
                    {
                        "cipher": "",
                        "connection_id": 134,
                        "server": "server2"
                    }
                ],
                "idle": 59.600000000000001,
                "log": [],
                "queries": [],
                "remote": "::ffff:127.0.0.1",
                "state": "Session started",
                "user": "maxuser"
            },
            "id": "1",
            "links": {
                "self": "http://localhost:8989/v1/sessions/1"
            },
            "relationships": {
                "services": {
                    "data": [
                        {
                            "id": "RW-Split-Router",
                            "type": "services"
                        }
                    ],
                    "links": {
                        "related": "http://localhost:8989/v1/services/",
                        "self": "http://localhost:8989/v1/sessions/1/relationships/services"
                    }
                }
            },
            "type": "sessions"
        }
    ],
    "links": {
        "self": "http://localhost:8989/v1/sessions/"
    }
}
```

### Update a Session

```
PATCH /v1/sessions/:id
```

The request body must be a JSON object which represents the new configuration of
the session. The `:id` must be a valid session ID that is active.

The `log_debug`, `log_info`, `log_notice`, `log_warning` and `log_error` boolean
parameters control whether the associated logging level is enabled:

```javascript
{
    "data": {
        "attributes": {
            "parameters": {
                "log_info": true
            }
        }
    }
}
```

The filters that a session uses can be updated by re-defining the filter
relationship of the session. This causes new filter sessions to be opened
immediately. The old filter session are closed and replaced with the new filter
session the next time the session is idle. The order in which the filters are
defined in the request body is the order in which the filters are installed,
similar to how the filter relationship for services behaves.

```javascript
{
    "data": {
        "attributes": {
            "relationships": {
                "filters": {
                    "data": [
                        { "id": "my-cache-filter" },
                        { "id": "my-log-filter" }
                    ]
                }
            }
        }
    }
}
```

#### Response

Session is modified:

`Status: 204 No Content`
