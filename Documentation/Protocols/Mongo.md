# MongoDB Protocol

The MongoDB protocol module provides access, subject to a number
limitations, to MariaDB databases for unalterered MongoDB applications.

TBW

## Authentication

Currently no authentication is supported in the communication between
the MongoDB application and MaxScale. That is, the connection string
should look like
```
const uri = "mongodb://127.0.0.1:4711"
```
without a username and password.

The credentials to be used when MaxScale connects to the database on behalf
of the MongoDB application must be specified in the configuration as `user`
and `password` parameters to the MongoDB protocol module.
```
[MongoDB-Listener]
type=listener
protocol=mongodbclient
mongodbclient.user=the_username
mongodbclient.password=the_password
port=4711
...
```
Note that all MongoDB applications connecting to the MongoDB listener
port will use the same credentials when the MariaDB database is accessed;
in that respect it is not possible to distinguish one MongoDB application
from another.

## Commands

The following lists all MongoDB commands that are supported.

### Find - https://docs.mongodb.com/manual/reference/command/find

The following fields are acted upon.

Field | Type | Description
--------------------------
find| string | The name of the target table.
projection | document | Optional. The projection specification to determine which fields to includein the returned documents.

All other fields are ignored.

#### Projection

The `projection` parameter determines which fields are returned in the matching documents.
The `projection` parameter takes a document of the following form:
```
{ <field1>: <value>, <field2>: <value> ... }
```

If a `projection` document is not provided of if it is empty, the entire document
will be returned.

Projection | Description
------------------------
`<field>: <1 or true>` | Specifies the inclusion of a field.
`<field>: <0 or false>` | Specifies the exclusion of a field.

##### Embedded Field Specification
For fields in an embedded documents, the field can be specified using:

   * _dot notation_; e.g. `"field.nestedfield": <value>`

Specifying fields in embedded documents using nested form is not supported.

##### `_id` Field Projection
The `_id` field is included in the returned documents by default unless you
explicitly specify `_id: 0` in the projection to suppress the field.

#### Inclusion or Exclusion
A `projection` cannot contain both include and exclude specifications,
with the exception of the `_id` field:

   * In projections that _explicitly_ include fields, the `_id` field is the only field that can be explicitly excluded.
   * In projections that _explicitly_ excludes fields, the `_id` field is the only field that can be explicitly include; however, the `_id` field is included by default.

*NOTE* Currently `_id` is the only field that can be excluded, and _only_
if other fields are exlicitly included.
*NOTE* Currently exclusion of other fields but `_id` is currently not supported.

### Insert - https://docs.mongodb.com/manual/reference/command/insert

The following fields are acted upon.

Field | Type | Description
--------------------------
insert| string | The name of the target table.
documents | array | An array of one or more documents to insert to the table.

All other fields are ignored.

The assumption is that there exists a table with the specified name and that
it has two columns; `id` of type `TEXT` and `doc` of type `JSON`.
From each document the _id_ is extracted, whereafter the id and the document
converted to JSON are inserted to the table.

Currently all documents are inserted using a single statement, so either all
documents will be inserted or none will be.

### Delete - https://docs.mongodb.com/manual/reference/command/insert

The following fields are acted upon.

Field | Type | Description
--------------------------
insert| string | The name of the target table.
deletes | array | An array of one document that describes what to delete.

All other fields are ignored.

**NOTE** Currently the `deletes` array can contain exactly one documents.
