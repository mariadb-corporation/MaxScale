# Admin User Resource

Admin users represent administrative users that are able to query and change
MaxScale's configuration.

## Resource Operations

### Get network user

Get a single network user. The The _:name_ in the URI must be a valid network
user name.

```
GET /v1/users/inet/:name
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/users/inet/my-user"
    },
    "data": {
        "id": "my-user",
        "type": "inet",
        "attributes": {
            "account": "admin"
        },
        "relationships": {
            "self": "http://localhost:8989/v1/users/inet/my-user"
        }
    }
}
```

### Get all network users

Get all network users.

```
GET /v1/users/inet
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/users/inet"
    },
    "data": [
        {
            "id": "my-user",
            "type": "inet",
            "attributes": {
                "account": "admin"
            },
            "relationships": {
                "self": "http://localhost:8989/v1/users/inet/my-user"
            }
        }
    ]
}
```

### Get enabled UNIX account

Get a single enabled UNIX account. The The _:name_ in the URI must be a valid
UNIX account name that has been enabled.

```
GET /v1/users/unix/:name
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/users/unix/maxscale"
    },
    "data": {
        "id": "maxscale",
        "type": "unix",
        "attributes": {
            "account": "basic"
        },
        "relationships": {
            "self": "http://localhost:8989/v1/users/unix/maxscale"
        }
    }
}
```

### Get all enabled UNIX accounts

Get all enabled UNIX accounts.

```
GET /v1/users/unix
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/users/unix"
    },
    "data": [
        {
            "id": "maxscale",
            "type": "unix",
            "attributes": {
                "account": "admin"
            },
            "relationships": {
                "self": "http://localhost:8989/v1/users/unix/maxscale"
            }
        }
    ]
}
```

### Get all users

Get all administrative users. This fetches both network users and local UNIX
accounts.

```
GET /v1/users
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/users/"
    },
    "data": [ // List of all users
        {
            "id": "my-user",
            "type": "inet", // A network user
            "attributes": {
                "account": "admin"
            },
            "relationships": {
                "self": "http://localhost:8989/v1/users/inet/my-user"
            }
        },
        {
            "id": "maxscale",
            "type": "unix", // A local UNIX account
            "attributes": {
                "account": "admin"
            },
            "relationships": {
                "self": "http://localhost:8989/v1/users/unix/maxscale"
            }
        }
    ]
}
```

### Create a network user

Create a new network user.

```
POST /v1/users/inet
```

The request body must fulfill the following requirements.

- The `/data/id`, `/data/type`, `/data/attributes/account` and
  `/data/attributes/password` fields must be defined.
- The `/data/id` field defines the name of the account
- The `/data/attributes/password` field defines the password for this user.
- The `/data/attributes/account` field should be set to `admin` for
  administrative users and `basic` to read-only users.
- The value of the `/data/type` field must always be `inet`.

Here is an example request body defining the network user _my-user_ with the
password _my-password_ that is allowed to execute only read-only operations.

```javascript
{
    "data": {
        "id": "my-user",
        "type": "inet",
        "attributes": {
            "password": "my-password",
            "account": "basic"
        }
    }
}
```

#### Response

```
Status: 204 No Content
```

### Enable a UNIX account

This enables an existing UNIX account on the system for administrative
operations.

```
POST /v1/users/unix
```

The request body must fulfill the following requirements.

- The `/data/id`, `/data/type` and `/data/attributes/account` fields must be defined.
- The `/data/id` field defines the name of the account
- The `/data/attributes/account` field should be set to `admin` for
  administrative users and `basic` to read-only users.
- The value of the `/data/type` field must always be `unix`.

Here is an example request body enabling the UNIX account _jdoe_ for read-only operations.

```javascript
{
    "data": {
        "id": "jdoe",
        "type": "unix"
        "attributes": {
            "account": "basic"
        }
    }
}
```

#### Response

```
Status: 204 No Content
```

### Delete a network user

The _:name_ part of the URI must be a valid user name.

```
DELETE /v1/users/inet/:name
```

#### Response

```
Status: 204 No Content
```

### Disable a UNIX account

The _:name_ part of the URI must be a valid user name.

```
DELETE /v1/users/unix/:name
```

#### Response

```
Status: 204 No Content
```
