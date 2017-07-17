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
        "self": "http://localhost:8989/v1/sessions/9"
    },
    "data": {
        "id": "9",
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
            "state": "Session ready for routing",
            "user": "maxuser",
            "remote": "::ffff:127.0.0.1",
            "connected": "Mon Jul 17 11:10:39 2017",
            "idle": 23.800000000000001
        },
        "links": {
            "self": "http://localhost:8989/v1/sessions/9"
        }
    }
}
```

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
            "id": "9",
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
                "state": "Session ready for routing",
                "user": "maxuser",
                "remote": "::ffff:127.0.0.1",
                "connected": "Mon Jul 17 11:10:39 2017",
                "idle": 62.899999999999999
            },
            "links": {
                "self": "http://localhost:8989/v1/sessions/9"
            }
        },
        {
            "id": "10",
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
                "state": "Session ready for routing",
                "user": "skysql",
                "remote": "::ffff:127.0.0.1",
                "connected": "Mon Jul 17 11:11:37 2017",
                "idle": 5.2000000000000002
            },
            "links": {
                "self": "http://localhost:8989/v1/sessions/10"
            }
        }
    ]
}
```
