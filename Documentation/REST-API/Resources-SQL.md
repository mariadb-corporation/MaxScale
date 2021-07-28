# SQL Resource

The SQL resource represents a database connection.

## SQL Connection Interface

The following endpoints provide a simple REST API interface for executing
SQL queries on servers and services in MaxScale.

This document uses the `:id` value in the URL to represent a connection ID and
the `:query_id` to represent a query ID. These values do not need to be manually
added as the relevant links are returned in the request body of each endpoint.

The endpoints use JSON Web Tokens to uniquely identify open SQL connections. A
connection token can be acquired with a `POST /v1/sql` request and can be used
with the `POST /v1/sql/:id/query`, `GET /v1/sql/:id/results/:query_id` and
`DELETE /v1/sql` endpoints. All of these endpoints accept a connection token in
the `token` parameter of the request:

```
POST /v1/sql/query?token=eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJhZG1pbiIsImV4cCI6MTU4MzI1NDE1MSwiaWF0IjoxNTgzMjI1MzUxLCJpc3MiOiJtYXhzY2FsZSJ9.B1BqhjjKaCWKe3gVXLszpOPfeu8cLiwSb4CMIJAoyqw
```

In addition to request parameters, the token can be stored in cookies in which
case they are automatically used by the REST API. For more information about
token storage in cookies, see the documentation for `POST /v1/sql`.

##  Request Parameters

All of the endpoints that operate on a single connection support the following
request parameters. The `GET /v1/sql` and `GET /v1/sql/:id` endpoints are an
exception as they ignore the current connection token.

- `token`

  - The connection token to use for the request. If provided, the value is
    unconditionally used even if a cookie with a valid token exists.

### Get one SQL connection

```
GET /v1/sql/:id
```

#### Response

Response contains the requested resource.

`Status: 200 OK`

```javascript
{
    "data": {
        "id": "5",
        "links": {
            "related": "http://localhost:8989/v1/sql/5/queries/",
            "self": "http://localhost:8989/v1/sql/5/"
        },
        "type": "sql"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/5/"
    },
    "meta": {
        "token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiI1IiwiZXhwIjoxNjIwMjM1Mzc3LCJpYXQiOjE2MjAyMDY1NzcsImlzcyI6Im14cy1xdWVyeSJ9.2CJ8DsEPbGlvs2DrBUC6FJA64VMSU8kbX1U4FSu2-OY"
    }
}
```

### Get all SQL connections

```
GET /v1/sql
```

#### Response

Response contains a resource collection with all the open SQL connections.

`Status: 200 OK`

```javascript
{
    "data": [
        {
            "id": "10",
            "links": {
                "related": "http://localhost:8989/v1/sql/10/queries/",
                "self": "http://localhost:8989/v1/sql/10/"
            },
            "type": "sql"
        },
        {
            "id": "11",
            "links": {
                "related": "http://localhost:8989/v1/sql/11/queries/",
                "self": "http://localhost:8989/v1/sql/11/"
            },
            "type": "sql"
        }
    ],
    "links": {
        "self": "http://localhost:8989/v1/sql/"
    }
}
```

### Open SQL connection to server

```
POST /v1/sql
```

The request body must be a JSON object consisting of the following fields:

- `target`

  - The object in MaxScale to connect to. This is a mandatory value and the
    given value must be the name of a valid server, service or listener in
    MaxScale.

- `user`

  - The username to use when creating the connection. This is a mandatory value.

- `password`

  - The password for the user. This is a mandatory value.

- `db`

  - The default database for the connection. By default the connection will have
    no default database.

- `timeout`

  - Connection timeout in seconds. The default connection timeout is 10
    seconds. This controls how long the SQL connection creation can take before
    an error is returned.

Here is an example request body:

```
{
    "user": "jdoe",
    "password": "my-s3cret",
    "target": "server1",
    "db": "test",
    "timeout": 15
}
```

The response will contain the new connection with the token stored at
`meta.token`. If the request uses the `persist=yes` request parameter, the token
is stored in cookies instead of the metadata object and the response body will
not contain the token.

The location of the newly created connection will be stored at `links.self` in
the response body as well as in the `Location` header.

The token must be given to all subsequent requests that use the connection. It
must be either given in the `token` parameter of a request or it must be stored
in the cookies. If both a `token` parameter and a cookie exist at the same time,
the `token` parameter will be used instead of the cookie.

####  Request Parameters

This endpoint supports the following request parameters.

- `persist`

  - Store the connection token in cookies instead of returning it as the response body.

    This parameter expects only one value, `yes`, as its argument. When
    `persist=yes` is set, the token is stored in two cookies,
    `conn_id_body_<id>` and `conn_id_sig_<id>` where the `<id>` part is replaced
    by the ID of the connection.

    The `conn_id_body_<id>` cookie contains the JWT header and claims sections
    and contains the connection ID in the `aud` value. This can be used to
    retrieve the connection ID from the cookies if the browser session is
    closed.

#### Response

Connection was opened:

`Status: 201 Created`

```javascript
{
    "data": {
        "id": "5",
        "links": {
                 // The "related" endpoint is the URL to the query endpoint for this connection.
            "related": "http://localhost:8989/v1/sql/5/queries/",
            "self": "http://localhost:8989/v1/sql/5/"
        },
        "type": "sql"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/5/"
    },
    "meta": {
        "token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiI1IiwiZXhwIjoxNjIwMjM1Mzc3LCJpYXQiOjE2MjAyMDY1NzcsImlzcyI6Im14cy1xdWVyeSJ9.2CJ8DsEPbGlvs2DrBUC6FJA64VMSU8kbX1U4FSu2-OY"
    }
}
```

Missing or invalid payload:

`Status: 403 Forbidden`

### Close an opened SQL connection

```
DELETE /v1/sql/:id
```

#### Response

Connection was closed:

`Status: 204 No Content`

Missing or invalid connection token:

`Status: 403 Forbidden`

### Execute SQL query

```
POST /v1/sql/:id/queries
```

The request body must be a JSON object with the value of the `sql` field set to
the SQL to be executed:

```
{
    "sql": "SELECT * FROM test.t1"
}
```

By default, the complete result is returned in the response body. If the SQL
query returns more than one result, the `results` array will contain all the
results.

The `results` array can have three types of objects: resultsets, errors, and OK
responses.

- A resultset consists of the `data` field with the result data stored as a two
  dimensional array. The names of the fields are stored in an array in the
  `fields` field. These types of results will be returned for any operation that
  returns rows (i.e. `SELECT` statements)

```javascript
{
    "data": {
        "attributes": {
            "results": [
                {
                    "data": [
                        [
                            1
                        ],
                        [
                            2
                        ],
                        [
                            3
                        ]
                    ],
                    "fields": [
                        "id"
                    ]
                }
            ],
            "sql": "select * from t1"
        },
        "id": "9-1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/9/queries/9-1/"
    }
}
```

- An error consists of an object with the `errno` field set to the MariaDB error
  code, the `message` field set to the human-readable error message and the
  `sqlstate` field set to the current SQLSTATE of the connection.

```javascript
{
    "data": {
        "attributes": {
            "results": [
                {
                    "errno": 1064,
                    "message": "You have an error in your SQL syntax; check the manual that corresponds to your MariaDB server version for the right syntax to use near 'table t1' at line 1",
                    "sqlstate": "42000"
                }
            ],
            "sql": "select syntax_error from table t1"
        },
        "id": "4-1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/4/queries/4-1/"
    }
}
```

- An OK response is returned for any result that completes successfully but not
  return rows (e.g. an `INSERT` or `UPDATE` statement). The `affected_rows`
  field contains the number of rows affected by the operation, the
  `last_insert_id` contains the auto-generated ID and the `warnings` field
  contains the number of warnings raised by the operation.

```javascript
{
    "data": {
        "attributes": {
            "results": [
                {
                    "affected_rows": 0,
                    "last_insert_id": 0,
                    "warnings": 0
                }
            ],
            "sql": "drop table t1"
        },
        "id": "6-1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/6/queries/6-1/"
    }
}
```

####  Request Parameters

This endpoint supports the following request parameters.

- `page[size]`

  - limit the number of rows read from the resultset. By default there is no
    limit. The value must be a positive number. If the number of rows returned
    by a resultset exceeds the given limit, the rest of the result will be
    available at the URL stored in `data.links.next` as well as in the
    `Location` header. If links do not exist, the result was smaller than the
    given limit.

#### Response

Query successfully executed:

`Status: 201 Created`

```javascript
{
    "data": {
        "attributes": {
            "results": [
                {
                    "affected_rows": 0,
                    "last_insert_id": 0,
                    "warnings": 0
                }
            ],
            "sql": "drop table t1"
        },
        "id": "6-1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/6/queries/6-1/"
    }
}
```

Invalid payload or missing connection token:

`Status: 403 Forbidden`

Fatal connection error:

`Status: 503 Service Unavailable`

- If the API returns this response, the connection to the database server was
  lost. The only valid action to take at this point is to close it with the
  `DELETE /v1/sql/:id` endpoint.
