# Admin User Resource

Admin users represent administrative users that are able to query and change
MaxScale's configuration.

## Resource Operations

### Get network user

```
GET /v1/users/inet/:name
```

Get a single network user. The The _:name_ in the URI must be a valid network
user name.

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/users/inet/my-user"
    },
    "data": {
        "id": "my-user", // Username
        "type": "inet", // User type
        "attributes": {
            "account": "admin" // Type of the user, "admin" for read-write operations, "basic" for read-only
        },
        "relationships": {
            "self": "http://localhost:8989/v1/users/inet/my-user"
        }
    }
}
```

### Get all network users

```
GET /v1/users/inet
```

Get all network users.

#### Response

`Status: 200 OK`

```javascript
// See `/v1/users/inet/` for a descriptions of the fields
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

```
GET /v1/users/unix/:name
```

Get a single enabled UNIX account. The The _:name_ in the URI must be a valid
UNIX account name that has been enabled.

#### Response

`Status: 200 OK`

```javascript
// See `/v1/users/inet/` for a descriptions of the fields
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

```
GET /v1/users/unix
```

Get all enabled UNIX accounts.

#### Response

`Status: 200 OK`

```javascript
// See `/v1/users/inet/` for a descriptions of the fields
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

```
GET /v1/users
```

Get all administrative users. This fetches both network users and local UNIX
accounts.

#### Response

`Status: 200 OK`

```javascript
// See `/v1/users/inet/` for a descriptions of the fields
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

```
POST /v1/users/inet
```

Create a new network user. The request body must define at least the
following fields.

* `data.id`
  * The username

* `data.type`
  * Type of the object, must be `inet`

* `data.attributes.password`
  * The password for this user

* `data.attributes.account`
  * Set to `admin` for administrative users and `basic` to read-only users

Only admin accounts can perform POST, PUT, DELETE and PATCH requests. If a basic
account performs one of the aforementioned request, the REST API will respond
with a `401 Unauthorized` error.

Here is an example request body defining the network user _my-user_ with the
password _my-password_ that is allowed to execute only read-only operations.

```javascript
{
    "data": {
        "id": "my-user", // The user to create
        "type": "inet", // The type of the user
        "attributes": {
            "password": "my-password", // The password to use for the user
            "account": "basic" // The type of the account
        }
    }
}
```

#### Response

```
Status: 204 No Content
```

### Enable a UNIX account

```
POST /v1/users/unix
```

This enables an existing UNIX account on the system for administrative
operations. The request body must define at least the following fields.

* `data.id`
  * The username

* `data.type`
  * Type of the object, must be `unix`

* `data.attributes.account`
  * Set to `admin` for administrative users and `basic` to read-only users

Here is an example request body enabling the UNIX account _jdoe_ for read-only operations.

```javascript
{
    "data": {
        "id": "jdoe", // Account name
        "type": "unix" // Account type
        "attributes": {
            "account": "basic" // Type of the user account in MaxScale
        }
    }
}
```

#### Response

```
Status: 204 No Content
```

### Delete a network user

```
DELETE /v1/users/inet/:name
```

The _:name_ part of the URI must be a valid user name.

#### Response

```
Status: 204 No Content
```

### Disable a UNIX account

```
DELETE /v1/users/unix/:name
```

The _:name_ part of the URI must be a valid user name.

#### Response

```
Status: 204 No Content
```
