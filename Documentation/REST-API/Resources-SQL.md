# SQL Resource

The SQL resource represents a database connection.

[TOC]

## SQL Connection Interface

The following endpoints provide a simple REST API interface for executing
SQL queries on servers and services in MaxScale.

This endpoint also supports executing SQL queries using an ODBC driver. The
results returned by connections that use ODBC drivers can differ from the ones
returned by normal SQL connections to objects in MaxScale.

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

## Request Parameters

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
        "attributes": {
            "seconds_idle": 0.0013705639999999999,
            "sql": null,
            "target": "server1",
            "thread_id": 10
        },
        "id": "96be0ffe-10fb-4ed1-8e66-a17ef1eea0fe",
        "links": {
            "related": "http://localhost:8989/v1/sql/96be0ffe-10fb-4ed1-8e66-a17ef1eea0fe/queries/",
            "self": "http://localhost:8989/v1/sql/96be0ffe-10fb-4ed1-8e66-a17ef1eea0fe/"
        },
        "type": "sql"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/96be0ffe-10fb-4ed1-8e66-a17ef1eea0fe/"
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
            "attributes": {
                "seconds_idle": 0.0010341230000000001,
                "sql": null,
                "target": "server1",
                "thread_id": 12
            },
            "id": "90761656-3352-420b-83e7-0dcef691552a",
            "links": {
                "related": "http://localhost:8989/v1/sql/90761656-3352-420b-83e7-0dcef691552a/queries/",
                "self": "http://localhost:8989/v1/sql/90761656-3352-420b-83e7-0dcef691552a/"
            },
            "type": "sql"
        },
        {
            "attributes": {
                "seconds_idle": 0.002397377,
                "sql": null,
                "target": "server1",
                "thread_id": 11
            },
            "id": "98a8b5c5-3632-4f0f-98bb-0dc440a3409a",
            "links": {
                "related": "http://localhost:8989/v1/sql/98a8b5c5-3632-4f0f-98bb-0dc440a3409a/queries/",
                "self": "http://localhost:8989/v1/sql/98a8b5c5-3632-4f0f-98bb-0dc440a3409a/"
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

  - The object to connect to. This is a mandatory value and the
    given value must be the name of a valid server, service or listener in
    MaxScale or the value `odbc` if an ODBC connection is being made.

- `user`

  - The username to use when creating the connection. This is a mandatory value
    when connecting to an object in MaxScale.

- `password`

  - The password for the user. This is a mandatory value when connecting to an
    object in MaxScale.

- `db`

  - The default database for the connection. By default the connection will have
    no default database. This is ignored by ODBC connections.

- `timeout`

  - Connection timeout in seconds. The default connection timeout is 10
    seconds. This controls how long the SQL connection creation can take before
    an error is returned. This is accepted by all connection types.

- `connection_string`

  - Connection string that defines the ODBC connection. This is a required value
    for ODBC type connections and is ignored by all other connection types.

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

And here is an example request that uses an ODBC driver to connect to a remote
server:

```
{
    "target": "odbc",
    "connection_string": "Driver=MariaDB;SERVER=127.0.0.1;UID=maxuser;PWD=maxpwd"
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

#### Request Parameters

This endpoint supports the following request parameters.

- `persist`

  - Store the connection token in cookies instead of returning it as the response body.

    This parameter expects only one value, `yes`, as its argument. When
    `persist=yes` is set, the token is stored in the `conn_id_sig_<id>` cookie
    where the `<id>` part is replaced by the ID of the connection.

- `max-age`

  - Sets the connection token maximum age in seconds. The default is
    `max-age=28800`. Only positive values are accepted and if a non-positive or
    a non-integer value is found, the parameter is ignored. Once the token age
    exceeds the configured maximum value, the token can no longer be used and a
    new connection must be created.

#### Response

Connection was opened:

`Status: 201 Created`

```javascript
{
    "data": {
        "attributes": {
            "seconds_idle": 7.6394000000000001e-5,
            "sql": null,
            "target": "server1",
            "thread_id": 13
        },
        "id": "f4e38d96-99b4-479e-ac36-5f3b437aff99",
        "links": {
            "related": "http://localhost:8989/v1/sql/f4e38d96-99b4-479e-ac36-5f3b437aff99/queries/",
            "self": "http://localhost:8989/v1/sql/f4e38d96-99b4-479e-ac36-5f3b437aff99/"
        },
        "type": "sql"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/f4e38d96-99b4-479e-ac36-5f3b437aff99/"
    },
    "meta": {
        "token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJmNGUzOGQ5Ni05OWI0LTQ3OWUtYWMzNi01ZjNiNDM3YWZmOTkiLCJleHAiOjE2ODk5NTA4MDQsImlhdCI6MTY4OTkyMjAwNCwiaXNzIjoibXhzLXF1ZXJ5Iiwic3ViIjoiZjRlMzhkOTYtOTliNC00NzllLWFjMzYtNWYzYjQzN2FmZjk5In0.gCKYl7XwwnMLjJbQT6UShDuK8aJ6gessmredQ1i0On4"
    }
}
```

Missing or invalid payload:

`Status: 400 Bad Request`

### Close an opened SQL connection

```
DELETE /v1/sql/:id
```

#### Response

Connection was closed:

`Status: 204 No Content`

Missing or invalid connection token:

`Status: 400 Bad Request`

### Reconnect an opened SQL connection

```
POST /v1/sql/:id/reconnect
```

Reconnects an existing connection. This can also be used if the connection to
the backend server was lost due to a network error.

The connection will use the same credentials that were passed to the `POST
/v1/sql` endpoint. The new connection will still have the same ID in the REST
API but will be treated as a new connection by the database. A reconnection
re-initializes the connection and resets the session state. Reconnections cannot
take place while a transaction is open.

#### Response

Reconnection was successful:

`Status: 204 No Content`

Reconnection failed or connection is already in use:

`Status: 503 Service Unavailable`

Missing or invalid connection token:

`Status: 400 Bad Request`

### Clone an existing SQL connection

```
POST /v1/sql/:id/clone
```

Clones an existing connection. This is done by opening a new connection using
the credentials and configuration from the given connection.

#### Request Parameters

This endpoint supports the same request parameters as the `POST /v1/sql`
endpoint.

#### Response

The response is identical to the one in the `POST /v1/sql` endpoint. In
addition, this endpoint can return the following responses.

Connection is already in use:

`Status: 503 Service Unavailable`

Missing or invalid connection token:

`Status: 400 Bad Request`

### Execute SQL query

```
POST /v1/sql/:id/queries
```

The request body must be a JSON object with the value of the `sql` field set to
the SQL to be executed:

```
{
    "sql": "SELECT * FROM test.t1",
    "max_rows": 1000
}
```

The request body must be a JSON object consisting of the following fields:

- `sql`

  - The SQL to be executed. If the SQL contain multiple statements, multiple
    results are returned in the response body.

- `max_rows`

  - The maximum number of rows returned in the response. By default this is 1000
    rows. Setting the value to 0 means no limit. Any extra rows in the result
    will be discarded.


By default, the complete result is returned in the response body. If the SQL
query returns more than one result, the `results` array will contain all the
results. If the `async=true` request option is used, the query is queued for
execution.

The `results` array can have three types of objects: resultsets, errors, and OK
responses.

- A resultset consists of the `data` field with the result data stored as a two
  dimensional array. The names of the fields are stored in an array in the
  `fields` field and the field types and other metadata are stored in the
  `metadata` field. These types of results will be returned for any operation
  that returns rows (i.e. `SELECT` statements)

```javascript
{
    "data": {
        "attributes": {
            "execution_time": 0.00028492799999999999,
            "results": [
                {
                    "complete": true,
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
                    ],
                    "metadata": [
                        {
                            "catalog": "def",
                            "decimals": 0,
                            "length": 11,
                            "name": "id",
                            "schema": "test",
                            "table": "t1",
                            "type": "LONG"
                        }
                    ]
                }
            ],
            "sql": "SELECT id FROM test.t1"
        },
        "id": "b7243d92-5bc6-4814-80fb-6772831ead4b.1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/b7243d92-5bc6-4814-80fb-6772831ead4b/queries/b7243d92-5bc6-4814-80fb-6772831ead4b.1/"
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
            "execution_time": 0.00012686699999999999,
            "results": [
                {
                    "errno": 1064,
                    "message": "You have an error in your SQL syntax; check the manual that corresponds to your MariaDB server version for the right syntax to use near 'TABLE test.t1' at line 1",
                    "sqlstate": "42000"
                }
            ],
            "sql": "SELECT syntax_error FROM TABLE test.t1"
        },
        "id": "621bacd9-48fd-436c-afda-b4e4d0d7b228.1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/621bacd9-48fd-436c-afda-b4e4d0d7b228/queries/621bacd9-48fd-436c-afda-b4e4d0d7b228.1/"
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
            "execution_time": 0.000474659,
            "results": [
                {
                    "affected_rows": 0,
                    "last_insert_id": 0,
                    "warnings": 0
                }
            ],
            "sql": "CREATE TABLE test.my_table(id INT)"
        },
        "id": "60005d40-c034-4aa3-94de-b15c14d9c91c.1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/60005d40-c034-4aa3-94de-b15c14d9c91c/queries/60005d40-c034-4aa3-94de-b15c14d9c91c.1/"
    }
}
```

It is also possible for the fields of the error response to be present in the
resultset response if the result ended with an error but still generated some
data. Usually this happens when query execution is interrupted but a partial
result was generated by the server.

#### Request Parameters

- `async`

  - If set to `true`, the query is queued for asynchronous execution and the
     results must be retrieved later from the URL stored in `links.self` field
     of the response. The HTTP response code is set to HTTP 202 Accepted if the
     query was successfully queued for execution.

#### Response

Query successfully executed:

`Status: 201 Created`

```javascript
{
    "data": {
        "attributes": {
            "execution_time": 0.00014767200000000001,
            "results": [
                {
                    "complete": true,
                    "data": [
                        [
                            1
                        ]
                    ],
                    "fields": [
                        "1"
                    ],
                    "metadata": [
                        {
                            "catalog": "def",
                            "decimals": 0,
                            "length": 1,
                            "name": "1",
                            "schema": "",
                            "table": "",
                            "type": "LONG"
                        }
                    ]
                }
            ],
            "sql": "SELECT 1"
        },
        "id": "5999b711-d190-4f0e-8322-db3ce3bd97a2.1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/5999b711-d190-4f0e-8322-db3ce3bd97a2/queries/5999b711-d190-4f0e-8322-db3ce3bd97a2.1/"
    }
}
```

Query queued for execution:

`Status: 202 Accepted`

```javascript
{
    "data": {
        "attributes": {
            "execution_time": 0.0,
            "sql": "SELECT 1"
        },
        "id": "3d23f7e0-6a83-4282-94a5-8a1089d56f72.1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/3d23f7e0-6a83-4282-94a5-8a1089d56f72/queries/3d23f7e0-6a83-4282-94a5-8a1089d56f72.1/"
    }
}
```

Invalid payload or missing connection token:

`Status: 400 Bad Request`

Fatal connection error:

`Status: 503 Service Unavailable`

- If the API returns this response, the connection to the database server was
  lost. The only valid action to take at this point is to close it with the
  `DELETE /v1/sql/:id` endpoint.

### Get Asynchronous Query Results

```
GET /v1/sql/:id/queries/:query_id
```

The results are only available if a `POST /v1/sql/:id/queries` was executed with
the `async` field set to `true`. The result of any asynchronous query can be
read multiple times. Only the latest result is stored: executing a new query
will cause the latest result to be erased. Results can be explicitly erased
with a `DELETE` request.

#### Response

Query successfully executed:

`Status: 201 Created`

```javascript
{
    "data": {
        "attributes": {
            "execution_time": 0.00011945,
            "results": [
                {
                    "complete": true,
                    "data": [
                        [
                            1
                        ]
                    ],
                    "fields": [
                        "1"
                    ],
                    "metadata": [
                        {
                            "catalog": "def",
                            "decimals": 0,
                            "length": 1,
                            "name": "1",
                            "schema": "",
                            "table": "",
                            "type": "LONG"
                        }
                    ]
                }
            ],
            "sql": "SELECT 1"
        },
        "id": "63ec5e96-2bfa-40a9-b631-425b4e3e993c.1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/63ec5e96-2bfa-40a9-b631-425b4e3e993c/queries/63ec5e96-2bfa-40a9-b631-425b4e3e993c.1/"
    }
}
```

Query not yet complete:

`Status: 202 Accepted`

No asynchronous results expected, invalid payload or missing connection token:

`Status: 400 Bad Request`

Fatal connection error:

`Status: 503 Service Unavailable`

- If the API returns this response, the connection to the database server was
  lost. The only valid action to take at this point is to close it with the
  `DELETE /v1/sql/:id` endpoint.

### Erase Asynchronous Query Results

```
DELETE /v1/sql/:id/queries/:query_id
```

Erases the latest result of an asynchronously executed query. All asynchronous
results are erased when the connection that owns them is closed.

#### Response

Result erased:

`Status: 200 OK`

Connection is busy or it was not found:

`Status: 503 Service Unavailable`

Missing connection token:

`Status: 400 Bad Request`

### Cancel a Query

```
POST /v1/sql/:id/cancel
```

This endpoint cancels the current query being executed by this connection. If no
query is being done and the connection is idle, no action is taken.

If the connection is busy but it is not executing a query, an attempt to cancel
is still made: in this case the results of this operation are undefined for ODBC
connections, for MariaDB connections this will cause a `KILL QUERY` command to
be executed.

#### Response

Query was canceled:

`Status: 200 OK`

Connection was not found:

`Status: 503 Service Unavailable`

Missing connection token:

`Status: 400 Bad Request`

### List ODBC Drivers

```
GET /v1/sql/odbc/drivers
```

Get the list of configured ODBC drivers found by the driver manager. The list of
drivers includes all drivers known to the driver manager for which an installed
library was found (i.e. `Driver` or `Driver64` in `/etc/odbcinst.ini` points to
a file).

#### Response

The response contains a resource collection with all available drivers.

`Status: 200 OK`

```javascript
{
    "data": [
        {
            "attributes": {
                "description": "ODBC for MariaDB",
                "driver": "/usr/lib/libmaodbc.so",
                "driver64": "/usr/lib64/libmaodbc.so",
                "fileusage": "1"
            },
            "id": "MariaDB",
            "type": "drivers"
        }
    ],
    "links": {
        "self": "http://localhost:8989/v1/sql/odbc/drivers/"
    }
}
```

### Prepare ETL Operation

```
POST /v1/sql/:id/etl/prepare
```

The ETL operation requires two connections: an ODBC connection to a remote
server (source connection) and a connection to a server in MaxScale (destination
connection). All ETL operations must be done on the ODBC connection.

The ETL operations require that the MariaDB ODBC driver is installed on the
MaxScale server. This driver is often available in the package manager of your
operating system but it can also be downloaded from the MariaDB
website. Installation instructions for installing the driver manually can be
found
[here](https://mariadb.com/kb/en/about-mariadb-connector-odbc/#installing-mariadb-connectorodbc-on-linux).

The request body must be a JSON object consisting of the following fields:

- `target`

  The target connection ID that defines the destination server. This must be the
  ID of a connection (i.e. `data.attributes.id` ) to a server in MaxScale
  created via the `POST /v1/sql/` endpoint.

- `type`

  The type of the ETL data source. The value must be a string with one of the
  following values:

    - `mariadb`

      Extract data from a MariaDB database.

    - `postgresql`

      Extract data from a PostgreSQL database. This requires that the PostgreSQL
      ODBC driver is installed on the MaxScale server. This driver is often
      available in the package manager of your operating system.

    - `generic`

      Extract data from a generic ODBC source. This uses the ODBC catalog
      functions to determine the table layout. The results provided by this are
      not as accurate as the specialized versions but it can serve as a good
      starting point from which manual modifications to the SQL can be done.

      This ETL type requires that the table catalog is provided at the top level
      with the `catalog` field. The meaning of the catalog differs between
      database implementations.

- `tables`

  An array of objects, each of which must define a `table` and a `schema`
  field. The `table` field defines the name of the table to be imported and the
  `schema` field defines the schema it is in. If the objects contain a value for
  `create`, `select` or `insert`, the SQL generation for that part is skipped.

- `connection_string`

  Extra connection string that is appended to the destination server's
  connection string. This connection will always use the MariaDB ODBC
  driver. The list of supported options can be found
  [here](https://mariadb.com/kb/en/about-mariadb-connector-odbc/#parameters).

- `threads`

  The number of parallel connections used during the ETL operation. By default
  the ETL operation will use up to 16 connections.

- `timeout`

  The connection and query timeout in seconds. By default a timeout of 30
  seconds is used.

- `create_mode`

  A string with either `normal`, `ignore` or `replace` which controls how tables
  are handled that already exist on the destination server.

  If left undefined or set to `normal`, the tables are created using a normal
  `CREATE TABLE` statement. This will cause an error to be reported if the table
  already exist and will prevent the ETL from proceeding past the object
  creation stage.

  If set to `ignore` the tables are created with `CREATE TABLE IF NOT EXISTS`
  which will ignore any existing tables and assume that they are compatible with
  the rest of the ETL operation. This mode can be used to continuously load data
  from an external source into MariaDB.

  If set to `replace`, the tables are created with `CREATE OR REPLACE TABLE`
  which will cause existing tables to be dropped if they exist. This is not a
  reversible process so caution should be taken when this mode is used.

- `catalog`

  The catalog for the tables. This is only used when `type` is set to
  `generic`. In all other cases this value is ignored.

Here is an example payload that prepares the table `test.t1` for extraction from
a MariaDB server.

```javascript
{
  "type": "mariadb",
  "target": "e2a56d2f-6514-4926-8dba-dca0c4ae3a86",
  "tables": [
    {
      "table": "t1",
      "schema": "test"
    }
  ]
}
```

The token for the source connection is provided the same way it is
provided for all other `/v1/sql` endpoints: in the `token` request
parameter or in the cookies. The destination connection token is
provided either in the `target_token` request parameter or as a cookie.

## Request Parameters

This endpoints supports the following additional request parameters.

- `target_token`

  - The connection token for the destination connection. If provided, the value
    is unconditionally used even if a cookie with a valid token exists for the
    destination connection.

#### Response

ETL operation prepared:

`Status: 202 Accepted`

Once complete, the `/v1/sql/:id/queries/:query_id` endpoint will return the
following result.

```javascript
{
    "data": {
        "attributes": {
            "execution_time": 0.0062226729999999997,
            "results": {
                "ok": true,
                "stage": "prepare",
                "tables": [
                    {
                        "create": "CREATE DATABASE IF NOT EXISTS `test`;\nUSE `test`;\nCREATE TABLE `t1` (\n  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,\n  `data` varchar(255) DEFAULT NULL,\n  UNIQUE KEY `id` (`id`)\n) ENGINE=InnoDB AUTO_INCREMENT=2 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci",
                        "insert": "INSERT INTO `test`.`t1` (`id`,`data`) VALUES (?,?)",
                        "schema": "test",
                        "select": "SELECT `id`,`data` FROM `test`.`t1`",
                        "table": "t1"
                    }
                ]
            },
            "sql": "ETL"
        },
        "id": "31dc09b7-ec09-4e6d-b098-e925f706233c.1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/31dc09b7-ec09-4e6d-b098-e925f706233c/queries/31dc09b7-ec09-4e6d-b098-e925f706233c.1/"
    }
}
```

Invalid payload or missing connection token:

`Status: 400 Bad Request`

### Start ETL Operation

```
POST /v1/sql/:id/etl/start
```

The behavior of this endpoint is identical to the preparation but instead of
preparing the operation, this endpoint will execute the prepared ETL and load
the data into MariaDB.

The intended way of doing an ETL operation is to first prepare it using the
`/v1/sql/:id/etl/prepare` endpoint to retrieve the SQL statements that define
the ETL operation. Then if the ETL preparation is successful, the
`data.attributes.results.tables` value from the response is used as the `tables`
value for the ETL start operation, done on the `/v1/sql/:id/etl/start` endpoint.

If any of the `create`, `select` or `insert` fields for a table in the `tables`
list have not been defined, the SQL will be automatically generated by querying
the source server, similarly to how the preparation generates the SQL
statements. This means that the preparation step is optional if there is no need
to adjust the automatically generated SQL.

The ETL operation can be canceled using the `/v1/sql/:id/cancel` endpoint which
will roll back the ETL operation. Any tables that were created during the ETL
operation are not deleted on the destination server and must be manually cleaned
up.

If the ETL fails to extract the SQL from the source server or an error was
encountered during table creation, the response will have the `"ok"` field set
to false. Both the top-level object as well as the individual tables can have
the `"error"` field set to the error message. This field is filled during the
ETL operation which means errors are visible even if the ETL is still ongoing.

During the ETL operation, tables that have been fully processed will have a
`"execution_time"` field. This field has the total time in seconds it took to
execute the data loading step.

#### Response

ETL operation started:

`Status: 202 Accepted`

Once complete, the `/v1/sql/:id/queries/:query_id` endpoint will return the
following result.

```javascript
{
    "data": {
        "attributes": {
            "execution_time": 0.0094386039999999997,
            "results": {
                "ok": true,
                "stage": "load",
                "tables": [
                    {
                        "create": "CREATE DATABASE IF NOT EXISTS `test`;\nUSE `test`;\nCREATE TABLE `t1` (\n  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,\n  `data` varchar(255) DEFAULT NULL,\n  UNIQUE KEY `id` (`id`)\n) ENGINE=InnoDB AUTO_INCREMENT=2 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci",
                        "execution_time": 0.0033923809999999999,
                        "insert": "INSERT INTO `test`.`t1` (`id`,`data`) VALUES (?,?)",
                        "rows": 1,
                        "schema": "test",
                        "select": "SELECT `id`,`data` FROM `test`.`t1`",
                        "table": "t1"
                    }
                ]
            },
            "sql": "ETL"
        },
        "id": "1391b67e-58a7-4be3-b686-2498cb3a0e06.1",
        "type": "queries"
    },
    "links": {
        "self": "http://localhost:8989/v1/sql/1391b67e-58a7-4be3-b686-2498cb3a0e06/queries/1391b67e-58a7-4be3-b686-2498cb3a0e06.1/"
    }
}
```

Invalid payload or missing connection token:

`Status: 400 Bad Request`
