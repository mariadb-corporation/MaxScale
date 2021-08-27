# NoSQL Protocol Module

The `nosqlprotocol` module allows a MariaDB server or cluster to be
used as the backend of an application using a MongoDB® client library.
Internally, all documents are stored in a table containing two columns;
an `id` column for the object id and a `doc` column for the document itself.

When the MongoDB® client application issues MongoDB protocol _commands_,
either directly or indirectly via the client library, they are transparently
converted into the equivalent SQL and executed against the MariaDB backend.
The MariaDB responses are then in turn converted into the format expected by
the MongoDB® client library and application.

[TOC]

# Configuring

There are a number of [parameters](#parameters) with which the behavior
of _nosqlprotocol_ can be adjusted. A minimal configuration looks
like:
```
[TheService]
type=service
...

[NoSQL-Listener]
type=listener
service=TheService
protocol=nosqlprotocol
nosqlprotocol.user=the_user
nosqlprotocol.password=the_password
port=17017
```
`nosqlprotocol.user` and `nosqlprotocol.password` specify the
credentials that will be used when accessing the backend database or
cluster. Note that the same credentials will be used for _all_ connecting
MongoDB® clients.

Since _nosqlprotocol_ is a _listener_, there must be a _service_ to which
the client requests will be sent. _Nosqlprotocol_ places no limitations
on what filters, routers or backends can be used.

To configure the same listener with MaxCtrl, the parameters must be passed in a
JSON object in the following manner:

```
maxctrl create listener TheService MongoDB-Listener --protocol=nosqlprotocol 'nosqlprotocol={"user":"the_user", "password": "the_password"}'
```

All the parameters that the nosqlprotocol module takes must be passed in the
same JSON object.

A complete example can be found at the [end](#example) of this document.

# Client Authentication

Currently no authentication is supported in the communication between
the MongoDB® client application and MaxScale. That is, when connecting, only
the host and port should be provided, but neither username nor password.
For instance, if the _MongoDB Node.JS Driver_ is used, then the connection
string should look like:
```
const uri = "mongodb://127.0.0.1:17017"
```

Similarly, if the _Mongo Shell_ is used, only the host and port should
be provided:
```
$ mongo --host 127.0.0.1 --port 17017
MongoDB shell version v4.4.1
...
>
```

# Client Library

As the goal of _nosqlprotocol_ is to implement, to the extent that it
is feasible, the wire protocol and the database commands the way MongoDB®
implements them, it should be possible to use any language specific driver.

However, during the development of _nosqlprotocol_, the _only_ client library
that has been verified to work is version 3.6 of _MongoDB Node.JS Driver_.

# Parameters

Using the following parameters, the behavior of _nosqlprotocol_ can be
adjusted. As they are not generic listener parameters, but specific to
_nosqlprotocol_ they must be qualified with the `nosqlprotocol`-prefix.

For instance:
```
[NoSQL-Listener]
type=listener
service=TheService
protocol=nosqlprotocol
nosqlprotocol.user=the_user
nosqlprotocol.password=the_password
nosqlprotocol.on_unknown_command=return_error
...
```

## `user`

   * Type: string
   * Mandatory: true

Specifies the _user_ to be used when connecting to the backend. Note that the
same _user_/_password_ combination will be used for all MongoDB® clients connecting
to the same listener port.

## `password`

   * Type: string
   * Mandatory: true

Specifies the _password_ to be used when connecting to the backend. Note that the
same _user_/_password_ combination will be used for all MongoDB® clients connecting
to the same listener port.

## `on_unknown_command`

   * Type: enumeration
   * Mandatory: false
   * Values: `return_error`, `return_empty`
   * Default: `return_error`

Specifies what should happen in case a clients sends an unrecognized command.

Enumeration values:

   * `return_error`: An error document is returned.
   * `return_empty`: An empty document is returned.

## `log_unknown_command`

   * Type: boolean
   * Mandatory: false
   * Default: `false`

Specifies whether an unknown command should be logged. This is primarily
for debugging purposes, to find out whether a client uses a command that
currently is not supported.

## `auto_create_databases`

   * Type: boolean
   * Mandatory: false
   * Default: `true`

Specifies whether databases should automatically be created, as needed.

Note that setting this parameter to `true`, without also setting
`auto_create_tables` to `true`, has no effect at all.

## `auto_create_tables`

   * Type: boolean
   * Mandatory: false
   * Default: `true`

Specifies whether tables should automatically be created, as needed.

Note that this applies only if the relevant database already exists.
If a database should also be created if needed, then `auto_create_databases`
must also be set to `true`.

## `id_length`

   * Type: count
   * Mandatory: false
   * Range: `[35, 2048]`
   * Default: `35`

Specifies the length of the id column in tables that are automatically created.

## `ordered_insert_behavior`

   * Type: enumeration
   * Mandatory: false
   * Values: `atomic`, `default`
   * Default: `default`

Enumeration values:

   * `default`: Each document is inserted using a _separate_ `INSERT`, either in a
     multi-statement or in a compound statement. Whether an error causes the remaining
     insertions to be aborted, depends on the value of `ordered` specified in the
     insert command.
   * `atomic`: If the value of `ordered` in the insert command is `true`
     (the default) then all documents are inserted using a _single_ `INSERT` statement,
     that is, either all insertions succeed or none will. If `ordered` is false, then
     the behavior is as in the `default` case.

What combination of `ordered_insert_behavior` and `ordered` (in the insert command
document) is used, has an impact on the performance. Please see the discussion at
[insert](#insert).

## `cursor_timeout`

   * Type: duration
   * Mandatory: false
   * Default: 60s

Specifies how long a cursor can be idle, that is, not accessed, before it is
automatically closed.

# Databases and Tables

By default, _nosqlprotocol_ automatically creates databases as needed.
The default behavior can be changed by setting `auto_create_databases` to
false. In that case, databases must manually be created.

Each MongoDB® _collection_ corresponds to a MariaDB table with the same name.
However, it is always possible to access a collection irrespective of whether
the corresponding table exists or not; it will simply appear to be empty.

Inserting documents into a collection, whose corresponding table does not
exist, succeeds, provided `auto_create_tables` is `true`, as the table will
in that case be created.

When _nosqlprotocol_ creates a table, it uses a statement like
```
CREATE TABLE name (id VARCHAR(35) AS (JSON_COMPACT(JSON_EXTRACT(doc, "$._id"))) UNIQUE KEY,
                   doc JSON,
                   CONSTRAINT id_not_null CHECK(id IS NOT NULL));
```
where the length of the `VARCHAR` is specified by the value of `id_length`,
whose default and minimum is 35.

*NOTE* If the tables are created manually, then the `CREATE` statement
*must* contain a similar `AS`-clause as the one above and *should* contain
a similar constraint.

Note that _nosqlprotocol_ does not in any way verify that the table
corresponding to a collection being accessed or modified does indeed
have the expected columns `id` and `doc` of the expected types, but it
simply uses the table, which will fail if the layout is not the expected
one.

To reduce the risk for confusion, the recommendation is to use a specific
database for tables that contain documents.

# Operators

The following operators are currently supported.

## Query and Projection Operators

### Comparison Query Operators

* $eq
* $gt
* $gte
* $in
* $lt
* $lte
* $ne
* $nin

### Logical Query Operators

* $and
* $not
* $nor
* $or

### Element Query Operators

* $exists
* $type

#### `$type`

When `$type` is used, it will be converted into a condition involving one or more
[JSON_TYPE](https://mariadb.com/kb/en/json_type/) comparisons. The following subset
of types can be used in `$type` queries:

Type | Number | Alias | MariaDB Type
-----|--------|-------|-------------
Double | 1 | "double" | `DOUBLE`
String | 2 | "string" | `STRING`
object | 3 | "object" | `OBJECT`
Array | 4 | "array" | `ARRAY`
Boolean | 5 | "bool" | `BOOLEAN`
32-bit integer | 16 | "int" | `INTEGER`

The _"number"_ alias is supported and will match values whose MariaDB type is
`DOUBLE` or `INTEGER`.

### Array Query Operators

* $all
* $elemMatch
* $size

#### `$elemMatch`
As arguments, only the operators `$eq` and `$ne` are supported.

## Update Operators

### Field Update Operators

* $set
* $unset

# Database Commands

The following commands are supported. At each command is specified
what fields are relevant for the command.

**All** non-listed fields are ignored; their presence or absence have no
impact, unless otherwise explicitly specified.

## Aggregation Commands

### count

The following fields are relevant.

Field | Type | Description
------|------|------------
count| string | The name of the collection to count.
query| document | Optional. A query that selects which documents to count in the collection
limit| integer | Optional. The maximum number of matching documents to return.
skip | integer | Optional. The number of matching documents to skip before returning results.

### distinct

The following fields are relevant.

Field | Type | Description
------|------|------------
distinct| string | The name of the collection to query for distinct values.
key | string | The field for which to return distinct values.
query| document | Optional. A query that selects which documents to count in the collection

## Query and Write Operation Commands

### delete

The following fields are relevant.

Field | Type | Description
------|------|------------
delete| string | The name of the target table.
deletes | array | An array of one or more delete statements to perform in the named collection.
ordered | boolean | Optional. If `true`, then when a delete statement fails, return without performing the remaining delete statements. If `false`, then when a delete statement fails, continue with the remaining delete statements, if any. Defaults to `true`.

Each element of the deletes array contains the following fields:

Field | Type | Description
------|------|------------
q | document | The query that matches documents to delete.
limit | integer | The number of matching documents to delete. Specify either a 0 to delete all matching documents or 1 to delete a single document.

### find

The following fields are relevant.

Field | Type | Description
------|------|------------
find| string | The name of the target table.
filter| document | Optional. The query predicate. If unspecified, then all documents in the collection will match the predicate.
sort | document | Optional. The sort specification for the ordering of the results.
projection | document | Optional. The projection specification to determine which fields to includein the returned documents.
skip | Positive integer | Optional. Number of documents to skip. Defaults to 0.
limit | Non-negative integer | Optional. The maximum number of documents to return. If unspecified, then defaults to no limit. A limit of 0 is equivalent to setting no limit.
batchSize | Non-negative integer | Optional. The number of documents to return in the first batch. Defaults to 101. A batchSize of 0 means that the cursor will be established, but no documents will be returned in the first batch.
singleBatch | boolean | Optional. Determines whether to close the cursor after the first batch. Defaults to false.

All other fields are ignored.

#### Projection

The `projection` parameter determines which fields are returned in the matching documents.
The `projection` parameter takes a document of the following form:
```
{ <field1>: <value>, <field2>: <value> ... }
```

If a `projection` document is not provided or if it is empty, the entire document
will be returned.

Projection | Description
-----------|------------
`<field>: <1 or true>` | Specifies the inclusion of a field.
`<field>: <0 or false>` | Specifies the exclusion of a field.

##### Embedded Field Specification
For fields in an embedded documents, the field can be specified using:

   * _dot notation_; e.g. `"field.nestedfield": <value>`

In particular, specifying fields in embedded documents using nested form
is not supported.

##### `_id` Field Projection
The `_id` field is included in the returned documents by default unless you
explicitly specify `_id: 0` in the projection to suppress the field.

#### Inclusion or Exclusion
A `projection` cannot contain both include and exclude specifications,
with the exception of the `_id` field:

   * In projections that _explicitly_ include fields, the `_id` field is the only field that can be explicitly excluded.
   * In projections that _explicitly_ excludes fields, the `_id` field is the only field that can be explicitly include; however, the `_id` field is included by default.

*NOTE* Currently `_id` is the only field that can be excluded, and _only_
if other fields are explicitly included.
*NOTE* Currently exclusion of other fields but `_id` is not supported.

#### Filtering by `_id`

Note that there is a significant difference between
```
> db.runCommand({find: "collection", filter: { _id: 4711 }});
```
and
```
> db.runCommand({find: "collection", filter: { _id: { $eq: 4711 }}});
```
In the former case the generated `WHERE` clause will be
```
... WHERE (id = '4711')
```
and in the latter
```
... WHERE (JSON_EXTRACT(doc, '$._id') = 4711)
```
That is, in the former case the _indexed_ column `id` will be used, in the
latter it will not.

### getLastError

The following fields are relevant.

Field | Type | Description
------|------|------------
getLastError | any | Ignored.

### getMore

The following fields are relevant.

Field | Type | Description
------|------|------------
getMore | long | The cursor id.
collection | string | The name of the collection over which the cursor is operating.
batchSize | positive integer | Optional. The number of documents to return in the batch.

### insert

The `insert` command inserts one or more documents into the table whose
name is the same as that of the collection. If the option `auto_create_tables`
is `true`, then the table is created if it does not already exist. If the
value is `false`, then the insert will fail unless the table already exists.

The following fields are relevant.

Field | Type | Description
------|------|------------
insert| string | The name of the target collection (i.e. table).
documents | array | An array of one or more documents to be inserted to the named collection.
ordered | boolean | Optional, with default being `true`. See below for description.

#### `ordered`
The impact of `ordered` is dependent upon the value of `ordered_insert_behavior`.

##### `default`
In this case `ordered` has the same impact as in MongoDB®. That is, if the value
is `true`, then when an insert of a document fails, return without inserting any
remaining documents listed in the inserts array. If `false`, then when an insert
of a document fails, continue to insert the remaining documents.

##### `atomic`
If `ordered` is `true`, then all documents will be inserted using a single
INSERT command. That is, if the insertion of any document fails, for instance,
due to a duplicate id, then no document will be inserted. If `ordered` is `false`,
then the behavior is identical with that of `default`.

#### Performance

What combination of `ordered_insert_behavior` and `ordered` is used, has an
impact on the performance.

`ordered_insert_behavior` | `ordered = true` | `ordered = false`
--------------------------|------------------|------------------
`default`                 | All documents are inserted within a compound statement, in a transaction containing as many `INSERT` statements as there are documents. | All documents are inserted in a single multi-statement transaction containing as many `INSERT IGNORE` statements as there are documents.
`atomic`                  | All documents are inserted using a single `INSERT` statement. | _Same as above_

Of these, `atomic + true` is the fastest and `atomic|default + false` the slowest,
being roughly twice as slow. The performance of 'default + true' is halfway between
the two.

### resetError

The following fields are relevant.

Field | Type | Description
------|------|------------
resetError | any | Ignored.

### update

The following fields are relevant.

Field | Type | Description
------|------|------------
update | string | The name of the target table.
updates | array | An array of documents that describe what to updated.

All other fields are ignored.

#### Update Statements

Each element of the updates array is an update statement document.
Each document contains the following fields:

Field | Type | Description
------|------|------------
q | document | The query that matches documents to update.
u | document | The modifications to apply. See _behavior_ below for details.
multi| boolean | Optional. If `true`, updates all documents that meet the query criteria. If `false` limit the update to one document that meets the query criteria. Defaults to `false`.

Note that currently it is possible to set `multi` to `true` in conjunction
with a _replacement-style_ update, even though MongoDB® rejects that.

All other fields are ignored, with the exception of `upsert` that if present
with the value of `true` will cause the command to fail.

##### Behavior

Currently only updating using _update operator expressions_ or with a
_replacement document_ is supported. In particular, updating using an
_aggregation pipeline_ is not supported.

###### Update with an _Update Operator Expressions_ document

The update statement field `u` can accept a document that only contains
[update operator](#update-operators) expressions. For example:
```
updates: [
   {
     q: <query>,
     u: { $set: { status: "D" } },
      ...
   },
   ...
]
```
In this case, the update command updates only the corresponding fields in the document.

###### Update with a _Replacement Document_

The update statement field `u` field can accept a _replacement document_,
i.e. the document contains only `field:value` expressions. For example:
```
updates: [
   {
      q: <query>,
      u: { status: "D", quantity: 4 },
      ...
   },
   ...
]
```
In this case, the update command replaces the matching document with the update document.
The update command can only replace a single matching document; i.e. the multi field
cannot be true.

**Note** If the replacement document contains an `_id` field, it will be ignored and the
document id will remain non-changed while the document otherwise is replaced. This is
different from MongoDB® where the presence of the `_id` field in the replacement document
causes an error, if the value is not the same as it is in the document being replaced.

## Replication Commands

### isMaster

The following fields are relevant.

Field | Type | Description
------|------|------------
isMaster | any | Ignored.

## Sessions Commands

### endSessions

The following fields are relevant.

Field | Type | Description
------|------|------------
endSessions | array | Ignored.

The following document will always be returned:
```
{ "ok" : 1 }
```

## Administration Commands

### create

The following fields are relevant.

Field | Type | Description
------|------|------------
create| string | The name of the collection to create.

### createIndexes

The following fields are relevant.

Field | Type | Description
------|------|------------
createIndexes| string | The collection for which to create indexes.

**NOTE** Currently it is not possible to create indexes, but the command
will nonetheless return success, provide the index specification passes
some rudimentary sanity checks. Note also that the collection will be
created if it does not exist.

### drop

The following fields are relevant.

Field | Type | Description
------|------|------------
drop| string | The name of the collection to drop.

### dropDatabase

The following fields are relevant.

Field | Type | Description
------|------|------------
dropDatabase | any | Ignored.

### dropIndexes

The following fields are relevant.

Field | Type | Description
------|------|------------
dropIndexes | any | Ignored.

**NOTE** Currently it is not possible to create indexes and thus there
will never be any indexes that could be dropped. However, provided the
specfied collection exists, dropping indexes will always succeed except
for an attempt to drop the built-in `_id_` index.

### killCursors

The following fields are relevant.

Field | Type | Description
------|------|------------
killCursors | string | The name of the collection.
cursors | array | The ids of the cursors to kill.

### listCollections

The following fields are relevant.

Field | Type | Description
------|------|------------
listCollections | any | Ignored.
nameOnly | boolean | Optional. A flag to indicate whether the command should return just the collection names and type or return both the name and other information.

Note that the command lists all collections (that is, tables) that are found
in the current database. The listed collections may or may not be suitable
for being accessed using _nosqlprotocol_.

### listDatabases

The following fields are relevant.

Field | Type | Description
------|------|------------
listDatabases | any | Ignored.
nameOnly | boolean | Optional. A flag to indicate whether the command should return just the database names, or return both database names and size information.

### listIndexes

The following fields are relevant.

Field | Type | Description
------|------|------------
listIndexes | string | The name of the collection.

**NOTE** As it currently is not possible to actually create indexes,
although an attempt to do so using `createIndexes` will succeed, the
result will always only contain information about the built-in
index `_id_`.

### renameCollection

The following fields are relevant.

Field | Type | Description
------|------|------------
renameCollection | string | The namespace of the collection to rename. The namespace is a combination of the database name and the name of the collection.
to | string | The new namespace of the collection. Moving a collection/table from one database to another succeeds provided the databases reside in the same filesystem.

## Diagnostic Commands

### buildInfo

The following fields are relevant.

Field | Type | Description
------|------|------------
buildInfo | any | Ignored.

The command returns a document containing the stable fields. In addition, there is a field `maxscale` whose value is the MaxScale version, expressed as a string.

### getCmdLineOpts

The following fields are relevant.

Field | Type | Description
------|------|------------
getCmdLineOpts | any | Ignored.

### getLog

The following fields are relevant.

Field | Type | Description
------|------|------------
getLog | string | `*`, `global` and `startupWarnings`

The command returns a document of the correct format, but _no_ actual log data will be returned.

### listCommands

The following fields are relevant.

Field | Type | Description
------|------|------------
listCommands | any | Ignored.

### ping

The following fields are relevant.

Field | Type | Description
------|------|------------
ping | any | Ignored.

### validate

The following fields are relevant.

Field | Type | Description
------|------|------------
validate | string | The name of the collection to validate.

The command does not actually perform any validation but for checking
that the collection exists. The response will contain in `nrecords`
the current number of documents/rows it contains.

### whatsmyuri

The following fields are relevant.

Field | Type | Description
------|------|------------
whatsmyri | any | Ignored.

This is an internal command, implemented only because the Mongo Shell uses it.

## Free Monitoring Commands

### getFreeMonitoringStatus

The following fields are relevant.

Field | Type | Description
------|------|------------
getFreeMonitoringStatus | any | Ignored.

The following document will always be returned:
```
{ "state" : "undecided", "ok" : 1 }
```

## MaxScale Specific Commands

### mxsCreateDatabase

#### Definition

##### **mxsCreateDatabase**

The 'mxsCreateDatabase' command creates a new database and must be run
against the `admin` database.

#### Syntax

The 'mxsCreateDatabase' has the following syntax:
```
db.adminCommand(
    {
       mxsCreateDatabase: <name>
    }
)
```
##### Command Fields

The command takes the following fields:

Field | Type | Description
------|------|------------
mxsCreateDatabase | string | The name of the database to be created.

##### Returns

If database creation succeeds, the command returns a document with the
single field `ok` whose value is `1`.

```
> db.adminCommand({mxsCreateDatabase: "db"});
{ "ok" : 1 }
```

If the database creation fails, the command returns an error document.
```
> db.adminCommand({mxsCreateDatabase: "db"});
{
	"ok" : 0,
	"errmsg" : "The database 'db' exists already.",
	"code" : 48,
	"codeName" : "NamespaceExists"
}
```

### mxsDiagnose

#### Definition

##### **mxsDiagnose**

The `mxsDiagnose` command provides diagnostics for any other command; that is, how
MaxScale will handle that command.

#### Syntax

The `mxsDiagnose` command has the following syntax:
```
db.runCommand(
    {
       mxsDiagnose: <command>
    }
)
```
##### Command Fields

The command takes the following fields:

Field | Type | Description
------|------|------------
mxsDiagnose | document | A command as provided to `db.runCommand(...)`.

##### Returns

The command returns a document that contains diagnostics of the command
provided as argument. For example:
```
> db.runCommand({mxsDiagnose: {ping:1}});
{ "kind" : "immediate", "response" : { "ok" : 1 }, "ok" : 1 }

> db.runCommand({mxsDiagnose: {find:"person", filter: { name: "Bob"}}});
{
  "kind" : "single",
  "sql" : "SELECT doc FROM `test`.`person` WHERE ( JSON_EXTRACT(doc, '$.name') = 'Bob') ",
  "ok" : 1
}

> db.runCommand({mxsDiagnose: {delete:"person", deletes: [{q: { name: "Bob"}, limit:0}, {q: {name: "Alice"}, limit:0}]}});
{
  "kind" : "single",
  "sql" : [
    "DELETE FROM `test`.`person` WHERE ( JSON_EXTRACT(doc, '$.name') = 'Bob') ",
    "DELETE FROM `test`.`person` WHERE ( JSON_EXTRACT(doc, '$.name') = 'Alice') "
  ],
  "ok" : 1
}
```
`kind` specifies of what kind the command is; an _immediate_ command is one for
which MaxScale autonomously can generate the response, a _single_ command is one
where the command will cause a single SQL statement to be sent to the backend, and
a _multi_ command is one where potentially multiple SQL statements will be sent to
the backend.

If the command is _immediate_ then there will be a field `response` containing
the actual response of the command, if the command is _single_ then there will be
a field `sql` containing the actual statement that would have been sent to the backend,
and if the command is _multi_ then there will be a field `sql` containing an array
of statements that would have been sent to the backend.

If an error occurs while the command is being diagnosed, then there will be no
`response` field but an `error` field whose value is an error document. Note that
the value of `ok` will always be 1.

### mxsGetConfig

#### Definition

#### **mxsGetConfig**

The `mxsGetConfig` command returns the current configuration of the session
and must be run against the 'admin' database.

#### Syntax

The `mxsGetConfig` has the following syntax:
```
db.runCommand(
    {
        mxsGetConfig: <any>
    });
```
##### Command Fields

The command takes the following fields:

Field | Type | Description
------|------|------------
mxsGetConfig | <any> | Ignored.

##### Returns

The command returns a document that contains the current configuration of
the session. For example:
```
> db.runCommand({mxsGetConfig: 1});
{
	"config" : {
		"on_unknown_command" : "return_error",
		"auto_create_tables" : true,
		"id_length" : 35
	},
	"ok" : 1
}
```

### mxsSetConfig

#### Definition

#### **mxsSetConfig**

The `mxsSetConfig` command changes the configuration of the session
and must be run against the 'admin' database.

Note that the changes only affect the current session and are **not**
persisted.

#### Syntax

The `mxsSetConfig` has the following syntax:
```
db.runCommand(
    {
        mxsSetConfig: document
    });
```
##### Command Fields

The command takes the following fields:

Field | Type | Description
------|------|------------
mxsSetConfig | document | A document specifying the configuration.

The document takes the following fields:

Field | Type | Description
------|------|------------
on_unknown_command | string | Either `"return_error"` or `"return_empty"`
auto_create_tables | boolean | Whether tables should be created as needed.
id_length | integer | `id` column `VARCHAR` size in created tables.

##### Returns

The command returns a document that contains the changed configuration of
the session. For example:
```
> db.runCommand({mxsGetConfig: 1});
{
	"config" : {
		"on_unknown_command" : "return_error",
		"auto_create_tables" : true,
		"id_length" : 35
	},
	"ok" : 1
}
> db.runCommand({mxsSetConfig: { auto_create_tables: false}});
{
	"config" : {
		"on_unknown_command" : "return_error",
		"auto_create_tables" : false,
		"id_length" : 35
	},
	"ok" : 1
}

```

# Object Id

When a document is created, an id of type `ObjectId` will be autogenerated by
the MongoDB® client library. If the id is provided explicitly, by assigning a
value to the `_id` field, the value must be an `ObjectId`, a string or an
integer.

# Limitations

Currently, the generated SQL must fit in one `COM_QUERY` packet, that is,
it can at most be 16777210 bytes.

# Example

The following is a minimal setup for getting _nosqlprotocol_ up and
running. It is assumed the reader knows how to configure MaxScale for
normal use. If not, please start with the
[MaxScale tutorial](../Tutorials/MaxScale-Tutorial.md).
Note that as _nosqlprotocol_ is the first component in the MaxScale
routing chain, it can be used with all routers and filters.

## Configuring MaxScale

In the following it is assumed that MaxScale already has been configured
for normal use and that there exists a _service_ `[TheService]`.
```
[TheService]
type=service
...

[NoSQL-Listener]
type=listener
service=TheService
protocol=nosqlprotocol
nosqlprotocol.user=the_user
nosqlprotocol.password=the_password
port=17017
```
The values `the_user` and `the_password` must be replaced with the
actual credentials to be used for every MongoDB® client that connects.

If MaxScale is now started, the following entry should appear in the
log file.
```
... notice : (NoSQL-Listener); Listening for connections at [127.0.0.1]:17017
```

## MongoDB® Shell

The mongo Shell is a powerful tool with which to access and manipulate a
MongoDB database. It is part of the MongoDB® package. Having the native
MongoDB database installed is convenient, as it makes it easy to ascertain
whether a problem is due to _nosqlprotocol_ not fully implementing something
or due to the API not being used in the correct fashion.

With the _mongo shell_, all that is needed is to invoke it with the port
_nosqlprotocol_ is listening on:
```
$ mongo --port 17017
MongoDB shell version v4.4.1
connecting to: mongodb://127.0.0.1:17017/?compressors=disabled&gssapiServiceName=mongodb
Implicit session: session { "id" : UUID("694f3eed-329f-487a-8d73-9a2d4cf82d62") }
MongoDB server version: 4.4.1
---
        ...
---
>
```
If the shell prompt appears, then a connection was successfully
established and the shell can be used.
```
> db.runCommand({insert: "collection", documents: [{_id: 1, "hello": "world"}]});
{ "n" : 1, "ok" : 1 }
```
The `db` variable is implicitly available, and refers by default to
the `test` database.

The command inserted a document into the collection called `collection`.
The table corresponding to that collection is created implicitly because
the default value of `auto_create_tables` is `true`. Here, the object id
is specified explicitly, but there is no need for that, as one will be
created if needed.

To check whether the documents was inserted into the collection, the
`find` command can be issued:
```
> db.runCommand({find: "collection"});
{
    "cursor" : {
        "firstBatch" : [
            {
                "_id" : 1,
                "hello" : "world"
            }
        ],
        "id" : NumberLong(0),
        "ns" : "test.collection"
    },
    "ok" : 1
}
```
As can be seen, the document was indeed inserted into the collection

With the `mysql` shell, the content of the actual table can be checked.
```
MariaDB [(none)]> select * from test.collection;
+------+------------------------------------+
| id   | doc                                |
+------+------------------------------------+
| 1.0  | { "_id" : 1.0, "hello" : "world" } |
+------+------------------------------------+
```
The collection `collection` is represented by a table `collection` with
the two colums `id` and `doc`. `id` is a virtual column whose content is
the value of the `_id` field of the document in the `doc` column.

All MongoDB® commands that _mongdbprotocol_ support (but for the ones that
do not require database access), basically access or manipulate the
content in the `doc` column using the
[JSON functions](https://mariadb.com/kb/en/json-functions/) of MariaDB.

From within the mongo shell itself it is easy to find out just what SQL
a particular MongoDB command is translated into.

For instance, the SQL that the insert command with which the document was
added can be found out like:
```
> db.runCommand({mxsDiagnose: {insert: "collection", documents: [{_id: 1, "hello": "world"}]}});
{
	"kind" : "multi",
	"sql" : [
		"INSERT INTO `test`.`collection` (doc) VALUES ('{ \"_id\" : 1.0, \"hello\" : \"world\" }')"
	],
	"ok" : 1
}
```
Similarily, the SQL of the `find` command can be find out like:
```
> db.runCommand({mxsDiagnose: {find: "collection"}});
{
	"kind" : "single",
	"sql" : "SELECT doc FROM `test`.`collection` ",
	"ok" : 1
}
```
The returned SQL can be directly pasted at the `mysql` prompt, which is
quite convenient in case the MongoDB® command does not behave as expected.

## MongoDB® Node.JS Driver

As all client libraries implement and depend on the the MongoDB® wire protocol,
all client libraries should work with _nosqlprotocol_. However, the
only client library that has been used and that has been verified to work
is version 3.6 of the _MongoDB Node.JS Driver_.

In principle, the only thing that needs to be altered in an existing
program using the library is to change the uri string that typically
is something like
```
const uri = "mongodb+srv://<user>:<password>@<cluster-url>?writeConcern=majority";
```
to
```
const uri = "mongodb://<maxscale-ip>:17017";
```
with the assumption that the default _nosqlprotocol_ port is used.

In practice, additional modifications may be needed since _nosqlprotocol_
does not implement all commands and does not in all cases implement the
full functionality of the commands that it supports.

### Inserting a Document

Store the following into a file called `insert.js`.
```
const { MongoClient } = require("mongodb");

const uri = "mongodb://127.0.0.1:17017";

const client = new MongoClient(uri, { useUnifiedTopology: true });
async function run() {
  try {
    await client.connect();
    const database = client.db("mydb");
    const movies = database.collection("movies");
    // create a document to be inserted
    const movie = { title: "Apocalypse Now", director: "Francis Ford Coppola" };
    const result = await movies.insertOne(movie);
    console.log(
      `${result.insertedCount} documents were inserted with the _id: ${result.insertedId}`,
    );
  } finally {
    await client.close();
  }
}
run().catch(console.dir);
```
Then, run the program like
```
$ nodejs insert.js
1 documents were inserted with the _id: 60afca73bf486114e3fb48b8
```
As the id is not explicitly provided, it will not be the same.

### Finding a Document

Store the following into a file called `find.js`.
```
const { MongoClient } = require("mongodb");

const uri = "mongodb://127.0.0.1:17017";

const client = new MongoClient(uri, { useUnifiedTopology: true });
async function run() {
  try {
    await client.connect();
    const database = client.db("mydb");
    const movies = database.collection("movies");
    // Query for a movie that has the title 'Apocalypse Now'
    const query = { title: "Apocalypse Now" };
    const options = {
      // Include only the 'director' field in the returned document
      projection: { _id: 0, director: 1 },
    };
    const movie = await movies.findOne(query, options);
    // Returns a document and not a cursor, so print directly.
    console.log(movie);
  } finally {
    await client.close();
  }
}
run().catch(console.dir);
```
Then, run the program like
```
$ nodejs find.js
{ director: 'Francis Ford Coppola' }
```
