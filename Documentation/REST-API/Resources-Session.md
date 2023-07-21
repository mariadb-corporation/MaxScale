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
            "connected": "Fri, 21 Jul 2023 06:46:44 GMT",
            "connections": [
                {
                    "cipher": "",
                    "connection_id": 9,
                    "server": "server1"
                }
            ],
            "idle": 5.0999999999999996,
            "io_activity": 16,
            "log": [],
            "memory": {
                "connection_buffers": {
                    "backends": {
                        "server1": {
                            "misc": 795,
                            "readq": 0,
                            "total": 795,
                            "writeq": 0
                        }
                    },
                    "client": {
                        "misc": 1040,
                        "readq": 65536,
                        "total": 66576,
                        "writeq": 0
                    },
                    "total": 67371
                },
                "exec_metadata": 0,
                "last_queries": 0,
                "sescmd_history": 697,
                "total": 68068,
                "variables": 0
            },
            "parameters": {
                "log_debug": false,
                "log_error": false,
                "log_info": false,
                "log_notice": false,
                "log_warning": false
            },
            "port": 55892,
            "queries": [],
            "remote": "::ffff:127.0.0.1",
            "state": "Session started",
            "thread": 1,
            "user": "maxuser"
        },
        "id": "1",
        "links": {
            "self": "http://localhost:8989/v1/sessions/1/"
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
                    "self": "http://localhost:8989/v1/sessions/1/relationships/services/"
                }
            }
        },
        "type": "sessions"
    },
    "links": {
        "self": "http://localhost:8989/v1/sessions/1/"
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
                "connected": "Fri, 21 Jul 2023 06:46:44 GMT",
                "connections": [
                    {
                        "cipher": "",
                        "connection_id": 9,
                        "server": "server1"
                    }
                ],
                "idle": 5.0999999999999996,
                "io_activity": 16,
                "log": [],
                "memory": {
                    "connection_buffers": {
                        "backends": {
                            "server1": {
                                "misc": 795,
                                "readq": 0,
                                "total": 795,
                                "writeq": 0
                            }
                        },
                        "client": {
                            "misc": 1040,
                            "readq": 65536,
                            "total": 66576,
                            "writeq": 0
                        },
                        "total": 67371
                    },
                    "exec_metadata": 0,
                    "last_queries": 0,
                    "sescmd_history": 697,
                    "total": 68068,
                    "variables": 0
                },
                "parameters": {
                    "log_debug": false,
                    "log_error": false,
                    "log_info": false,
                    "log_notice": false,
                    "log_warning": false
                },
                "port": 55892,
                "queries": [],
                "remote": "::ffff:127.0.0.1",
                "state": "Session started",
                "thread": 1,
                "user": "maxuser"
            },
            "id": "1",
            "links": {
                "self": "http://localhost:8989/v1/sessions/1/"
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
                        "self": "http://localhost:8989/v1/sessions/1/relationships/services/"
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

### Restart a Session

```
POST /v1/sessions/:id/restart
```

This endpoint causes the session to re-read the configuration from the
service. As a result of this, all backend connections will be closed and then
opened again. All router and filter sessions will be created again which means
that for modules that perform something whenever a new module session is opened,
this behaves as if a new session was started.

This endpoint can be used to apply configuration changes that were done after
the session was started. This can be useful for situations where the client
connections live for a long time and connections are not recycled often enough.

#### Response

Session is was restarted:

`Status: 204 No Content`

### Restart all Sessions

```
POST /v1/sessions/restart
```

This endpoint does the same thing as the `/v1/sessions/:id/restart` endpoint
except that it applies to all sessions.

#### Response

Session is was restarted:

`Status: 204 No Content`

### Kill a Session

```
DELETE /v1/sessions/:id
```

This endpoint causes the session to be forcefully closed.

#### Request Parameters

This endpoint supports the following request parameters.

- `ttl`

  - The time after which the session is killed. If this parameter is not given,
    the session is killed immediately. This can be used to give the session time
    to finish the work it is performing before the connection is closed.

#### Response

Session was killed:

`Status: 204 No Content`
