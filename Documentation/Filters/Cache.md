# Cache

This filter was introduced in MariaDB MaxScale 2.1.

## Overview
_Note that the cache is still experimental and that non-backward compatible
changes may be made._

The cache filter is a simple cache that is capable of caching the result of
SELECTs, so that subsequent identical SELECTs are served directly by MaxScale,
without the queries being routed to any server.

The cache will be used and populated in the following circumstances:
* There is _no_ explicit transaction active, that is, _autocommit_ is used,
* there is an _explicitly_ read-only transaction (that is,`START TRANSACTION
  READ ONLY`) active, or
* there is a transaction active and _no_ statement that modifies the database
  has been performed.

In practice, the last bullet point basically means that if a transaction has
been started with `BEGIN`, `START TRANSACTION` or `START TRANSACTION READ
WRITE`, then the cache will be used and populated until the first `UPDATE`,
`INSERT` or `DELETE` statement is encountered.

By default, it is *ensured* that the cache is **not** used in the following
circumstances:

* The `SELECT` uses any of the following functions: `BENCHMARK`,
  `CONNECTION_ID`, `CONVERT_TZ`, `CURDATE`, `CURRENT_DATE`, `CURRENT_TIMESTAMP`,
  `CURTIME`, `DATABASE`, `ENCRYPT`, `FOUND_ROWS`, `GET_LOCK`, `IS_FREE_LOCK`,
  `IS_USED_LOCK`, `LAST_INSERT_ID`, `LOAD_FILE`, `LOCALTIME`, `LOCALTIMESTAMP`,
  `MASTER_POS_WAIT`, `NOW`, `RAND`, `RELEASE_LOCK`, `SESSION_USER`, `SLEEP`,
  `SYSDATE`, `SYSTEM_USER`, `UNIX_TIMESTAMP`, `USER`, `UUID`, `UUID_SHORT`.
* The `SELECT` accesses any of the following fields: `CURRENT_DATE`,
  `CURRENT_TIMESTAMP`, `LOCALTIME`, `LOCALTIMESTAMP`
* The `SELECT` uses system or user variables.

In order to ensure that, all `SELECT` statements have to be parsed, which
carries a _significant_ performance cost. If it is known that there are no
such statements or that it does not matter even if they are cached, that
safety measure can be turned off. Please read [performance](#performance)
for more details.

## Limitations

All of these limitations may be addressed in forthcoming releases.

### Invalidation
Currently there is **no** cache invalidation, apart from _time-to-live_.

### Prepared Statements
Resultsets of prepared statements are **not** cached.

### Security
The cache is **not** aware of grants.

The implication is that unless the cache has been explicitly configured
who the caching should apply to, the presence of the cache may provide
a user with access to data he should not have access to.

Please read the section [Security](#security-1) for more detailed information.

## Configuration

The cache is simple to add to any existing service. However, some experimentation
may be required in order to find the configuration settings that provide
the maximum benefit.

```
[Cache]
type=filter
module=cache
hard_ttl=30
soft_ttl=20
rules=...
...

[Cached Routing Service]
type=service
...
filters=Cache
```

Each configured cache filter uses a storage of its own. That is, if there
are two services, each configured with a specific cache filter, then,
even if queries target the very same servers the cached data will not
be shared.

Two services can use the same cache filter, but then either the services
should use the very same servers _or_ a completely different set of servers,
where the used table names are different. Otherwise there can be unintended
sharing.


### Filter Parameters

The cache filter has no mandatory parameters but a range of optional ones.
Note that it is advisable to specify `max_size` to prevent the cache from
using up all memory there is, in case there is very litte overlap among the
queries.

#### `storage`

The name of the module that provides the storage for the cache. That
module will be loaded and provided with the value of `storage_options` as
argument. For instance:
```
storage=storage_inmemory
```
The default is `storage_inmemory`.

See [Storage](#storage-1) for what storage modules are available.

#### `storage_options`

A comma separated list of arguments to be provided to the storage module,
specified in `storage`, when it is loaded. Note that the needed arguments
depend upon the specific module. For instance,
```
storage_options=storage_specific_option1=value1,storage_specific_option2=value2
```

#### `hard_ttl`

_Hard time to live_; the maximum amount of time - in seconds - the cached
result is used before it is discarded and the result is fetched from the
backend (and cached). See also `soft_ttl` below.

```
hard_ttl=60
```
The default value is `0`, which means no limit.

#### `soft_ttl`

_Soft time to live_; the amount of time - in seconds - the cached result is
used before it is refreshed from the server. When `soft_ttl` has passed, the
result will be refreshed when the _first_ client requests the value.

However, as long as `hard_ttl` has not passed, _all_ other clients requesting
the same value will use the result from the cache while it is being fetched
from the backend. That is, as long as `soft_ttl` but not `hard_ttl` has passed,
even if several clients request the same value at the same time, there will be
just one request to the backend.
```
soft_ttl=60
```
The default value is `0`, which means no limit. If the value of `soft_ttl` is
larger than `hard_ttl` it will be adjusted down to the same value.

#### `max_resultset_rows`

Specifies the maximum number of rows a resultset can have in order to be
stored in the cache. A resultset larger than this, will not be stored.
```
max_resultset_rows=1000
```
The default value is `0`, which means no limit.

#### `max_resultset_size`

Specifies the maximum size of a resultset, for it to be stored in the cache.
A resultset larger than this, will not be stored. The size can be specified
as described [here](../Getting-Started/Configuration-Guide.md#sizes).
```
max_resultset_size=128Ki
```
The default value is `0`, which means no limit.

Note that the value of `max_resultset_size` should not be larger than the
value of `max_size`.

#### `max_count`

The maximum number of items the cache may contain. If the limit has been
reached and a new item should be stored, then an older item will be evicted.

Note that if `cached_data` is `thread_specific` then this limit will be
applied to each cache _separately_. That is, if a thread specific cache
is used, then the total number of cached items is #threads * the value
of `max_count`.
```
max_count=1000
```
The default value is `0`, which means no limit.

#### `max_size`

The maximum size the cache may occupy. If the limit has been reached and a new
item should be stored, then some older item(s) will be evicted to make space.
The size can be specified as described
[here](../Getting-Started/Configuration-Guide.md#sizes).

Note that if `cached_data` is `thread_specific` then this limit will be
applied to each cache _separately_. That is, if a thread specific cache
is used, then the total size is #threads * the value of `max_size`.
```
max_size=100Mi
```
The default value is `0`, which means no limit.

#### `rules`

Specifies the path of the file where the caching rules are stored. A relative
path is interpreted relative to the _data directory_ of MariaDB MaxScale.

```
rules=/path/to/rules-file
```

#### `cached_data`

An enumeration option specifying how data is shared between threads. The
allowed values are:

   * `shared`: The cached data is shared between threads. On the one hand
     it implies that there will be synchronization between threads, on
     the other hand that all threads will use data fetched by any thread.
   * `thread_specific`: The cached data is specific to a thread. On the
     one hand it implies that no synchonization is needed between threads,
     on the other hand that the very same data may be fetched and stored
     multiple times.

```
cached_data=thread_specific
```

Default is `shared`. See `max_count` and `max_size` what implication changing
this setting to `thread_specific` has.

#### `selects`

An enumeration option specifying what approach the cache should take with
respect to `SELECT` statements. The allowed values are:

   * `assume_cacheable`: The cache can assume that all `SELECT` statements,
     without exceptions, are cacheable.
   * `verify_cacheable`: The cache can *not* assume that all `SELECT`
     statements are cacheable, but must verify that.

```
selects=assume_cacheable
```

Default is `verify_cacheable`. In this case, the `SELECT` statements will be
parsed and only those that are safe for caching - e.g. do *not* call any
non-cacheable functions or access any non-cacheable variables - will be
subject to caching.

If `assume_cacheable` is specified, then all `SELECT` statements are
assumed to be cacheable and will be parsed *only* if some specific rule
requires that.

#### `debug`

An integer value, using which the level of debug logging made by the cache
can be controlled. The value is actually a bitfield with different bits
denoting different logging.

   * ` 0` (`0b00000`) No logging is made.
   * ` 1` (`0b00001`) A matching rule is logged.
   * ` 2` (`0b00010`) A non-matching rule is logged.
   * ` 4` (`0b00100`) A decision to use data from the cache is logged.
   * ` 8` (`0b01000`) A decision not to use data from the cache is logged.
   * `16` (`0b10000`) Higher level decisions are logged.

Default is `0`. To log everything, give `debug` a value of `31`.

```
debug=31
```

# Rules

The caching rules are expressed as a JSON object.

There are two decisions to be made regarding the caching; in what circumstances
should data be stored to the cache and in what circumstances should the data in
the cache be used.

In the JSON object this is visible as follows:

```
{
    store: [ ... ],
    use: [ ... ]
}
```

The `store` field specifies in what circumstances data should be stored to
the cache and the `use` field specifies in what circumstances the data in
the cache should be used. In both cases, the value is a JSON array containg
objects.

## When to Store

By default, if no rules file have been provided or if the `store` field is
missing from the object, the results of all queries will be stored to the
cache, subject to `max_resultset_rows` and `max_resultset_size` cache filter
parameters.

By providing a `store` field in the JSON object, the decision whether to
store the result of a particular query to the cache can be controlled in
a more detailed manner. The decision to cache the results of a query can
depend upon

   * the database,
   * the table,
   * the column, or
   * the query itself.

Each entry in the `store` array is an object containing three fields,

```
{
    "attribute": <string>,
    "op": <string>
    "value": <string>
}
```

where,
   * the _attribute_ can be `database`, `table`, `column` or `query`,
   * the _op_ can be `=`, `!=`, `like` or `unlike`, and
   * the _value_ a string.

If _op_ is `=` or `!=` then _value_ is used as a string; if it is `like`
or `unlike`, then _value_ is interpreted as a _pcre2_ regular expression.
Note though that if _attribute_ is `database`, `table` or `column`, then
the string is interpreted as a name, where a dot `.` denotes qualification
or scoping.

The objects in the `store` array are processed in order. If the result
of a comparison is _true_, no further processing will be made and the
result of the query in question will be stored to the cache.

If the result of the comparison is _false_, then the next object is
processed. The process continues until the array is exhausted. If there
is no match, then the result of the query is not stored to the cache.

Note that as the query itself is used as the key, although the following
queries
```
select * from db1.tbl
```
and
```
use db1;
select * from tbl
```
target the same table and produce the same results, they will be cached
separately. The same holds for queries like
```
select * from tbl where a = 2 and b = 3;
```
and
```
select * from tbl where b = 3 and a = 2;
```
as well. Although they conceptually are identical, there will be two
cache entries.

Note that if a column has been specified in a rule, then a statement
will match _irrespective_ of where that particular column appears.
For instance, if a rule specifies that the result of statements referring
to the the column _a_ should be cached, then the following statement will
match
```
select a from tbl;
```
and so will
```
select b from tbl where a > 5;
```

### Qualified Names

When using `=` or `!=` in the rule object in conjunction with `database`,
`table` and `column`, the provided string is interpreted as a name, that is,
dot (`.`) denotes qualification or scope.

In practice that means that if _attribute_ is `database` then _value_ may
not contain a dot, if _attribute_ is `table` then _value_ may contain one
dot, used for separating the database and table names respectively, and
if _attribute_ is `column` then _value_ may contain one or two dots, used
for separating table and column names, or database, table and column names.

Note that if a qualified name is used as a _value_, then all parts of the
name must be available for a match. Currently Maria DB MaxScale may not
always be capable of deducing in what table a particular column is. If
that is the case, then a value like `tbl.field` may not necessarily
be a match even if the field is `field` and the table actually is `tbl`.

### Implication of the _default_ database.

If the rules concerns the `database`, then only if the statement refers
to *no* specific database, will the default database be considered.

### Regexp Matching

The string used for matching the regular expression contains as much
information as there is available. For instance, in a situation like
```
use somedb;
select fld from tbl;
```
the string matched against the regular expression will be `somedb.tbl.fld`.

### Examples

Cache all queries targeting a particular database.
```
{
    "store": [
        {
            "attribute": "database",
            "op": "=",
            "value": "db1"
        }
    ]
}
```

Cache all queries _not_ targeting a particular table
```
{
    "store": [
        {
            "attribute": "table",
            "op": "!=",
            "value": "tbl1"
        }
    ]
}
```

That will exclude queries targeting table _tbl1_ irrespective of which
database it is in. To exclude a table in a particular database, specify
the table name using a qualified name.
```
{
    "store": [
        {
            "attribute": "table",
            "op": "!=",
            "value": "db1.tbl1"
        }
    ]
}
```

Cache all queries containing a WHERE clause
```
{
    "store": [
        {
            "attribute": "query",
            "op": "like",
            "value": ".*WHERE.*"
        }
    ]
}
```

Note that that will actually cause all queries that contain WHERE anywhere,
to be cached.

## When to Use

By default, if no rules file have been provided or if the `use` field is
missing from the object, all users may be returned data from the cache.

By providing a `use` field in the JSON object, the decision whether to use
data from the cache can be controlled in a more detailed manner. The decision
to use data from the cache can depend upon

   * the user.

Each entry in the `use` array is an object containing three fields,

```
{
    "attribute": <string>,
    "op": <string>
    "value": <string>
}
```

where,
   * the _attribute_ can be `user`,
   * the _op_ can be `=`, `!=`, `like` or `unlike`, and
   * the _value_ a string.

If _op_ is `=` or `!=` then _value_ is interpreted as a MariaDB account
string, that is, `%` means indicates wildcard, but if _op_ is `like` or
`unlike` it is simply assumed _value_ is a pcre2 regular expression.

For instance, the following are equivalent:

```
{
    "attribute": "user",
    "op": "=",
    "value": "'bob'@'%'"
}

{
    "attribute": "user",
    "op": "like",
    "value": "bob@.*"
}
```

Note that if _op_ is `=` or `!=` then the usual assumptions apply,
that is, a value of `bob` is equivalent with `'bob'@'%'`. If _like_
or _unlike_ is used, then no assumptions apply, but the string is
used verbatim as a regular expression.

The objects in the `use` array are processed in order. If the result
of a comparison is _true_, no further processing will be made and the
data in the cache will be used, subject to the value of `ttl`.

If the result of the comparison is _false_, then the next object is
processed. The process continues until the array is exhausted. If there
is no match, then data in the cache will not be used.

Note that `use` is relevant only if the query is subject to caching,
that is, if all queries are cached or if a query matches a particular
rule in the `store` array.

### Examples

Use data from the cache for all users except `admin` (actually `'admin'@'%'`),
regardless of what host the `admin` user comes from.
```
{
    "use": [
        {
            "attribute": "user",
            "op": "!=",
            "value": "admin"
        }
    ]
}
```
# Security

As the cache is not aware of grants, unless the cache has been explicitly
configured who the caching should apply to, the presence of the cache
may provide a user with access to data he should not have access to.

Suppose there is a table ``access`` that the user _alice_ has access to,
but the user _bob_ does not. If _bob_ tries to access the table, he will
get an error as reply:
```
MySQL [testdb]> select * from access;
ERROR 1142 (42000): SELECT command denied to user 'bob'@'localhost' for table 'access'
```
If we now setup caching for the table, using the simplest possible rules
file, _bob_ will get access to data from the table, provided he executes
a select identical with one _alice_ has executed.

For instance, suppose the rules look as follows:
```
{
    "store": [
        {
            "attribute": "table",
            "op": "=",
            "value": "access"
        }
    ]
}
```
If _alice_ now queries the table, she will get the result, which also will
be cached:
```
MySQL [testdb]> select * from access;
+------+------+
| a    | b    |
+------+------+
|   47 |   11 |
+------+------+
```
If _bob_ now executes the very same query, and the result is still in the
cache, it will be returned to him.
```
MySQL [testdb]> select current_user();
+----------------+
| current_user() |
+----------------+
| bob@127.0.0.1  |
+----------------+
1 row in set (0.00 sec)

MySQL [testdb]> select * from access;
+------+------+
| a    | b    |
+------+------+
|   47 |   11 |
+------+------+
```
That can be prevented, by explicitly declaring in the rules that the caching
should be applied to _alice_ only.
```
{
    "store": [
        {
            "attribute": "table",
            "op": "=",
            "value": "access"
        }
    ],
    "use": [
        {
            "attribute": "user",
            "op": "=",
            "value": "'alice'@'%'"
        }
    ]
}
```
With these rules in place, _bob_ is again denied access, since queries
targeting the table `access` will in his case not be served from the cache.

# Storage

## `storage_inmemory`

This simple storage module uses the standard memory allocator for storing
the cached data.
```
storage=storage_inmemory
```

## `storage_rocksdb`

This storage module is not built by default and is not included in the
MariaDB MaxScale packages.

This storage module uses RocksDB database for storing the cached data. The
directory where the RocksDB database will be created is by default created
into the _MaxScale cache_ directory, which usually is not on a RAM disk. For
maximum performance, you may want to explicitly place the RocksDB database
on a RAM disk.
```
storage=storage_rocksdb
```

### Parameters

#### `cache_directory`

Specifies the directory under which the filter instance specific RocksDB
databases will be placed. Note that at startup, each RocksDB database will
be deleted and recreated. That is, cache content will not be retained across
MaxScale restarts.

```
storage_options=cache_directory=/mnt/maxscale-cache
```

With the above setting a directory `/mnt/macscale-cache/storage_rocksdb` will
created, under which the actual instance specific cache directories are created.

#### `collect_statistics`

Specifies whether RocksDB should collect statistics that later can be queried
using `maxadmin`. It should be noted, though, that collecting RocksDB statistics
is not without a cost.
From the [RocksDB Documentation](https://github.com/facebook/rocksdb/wiki/Statistics)

_The overhead of statistics is usually small but non-negligible. We usually
observe an overhead of 5%-10%._

The value is a boolean and the default is `false`.

```
storage_options=collect_statistics=true
```

# Example

In the following we define a cache _MyCache_ that uses the cache storage module
`storage_inmemory` and whose _soft ttl_ is `30` seconds and whose _hard ttl_ is
`45` seconds. The cached data is shared between all threads and the maximum size
of the cached data is `50` mebibytes. The rules for the cache are in the file
`cache_rules.json`.

### Configuration
```
[MyCache]
type=filter
module=cache
storage=storage_inmemory
soft_ttl=30
hard_ttl=45
cached_data=shared
max_size=50Mi
rules=cache_rules.json

[MyService]
type=service
...
filters=MyCache
```

### `cache_rules.json`
The rules specify that the data of the table `sbtest` should be cached.

```
{
    "store": [
        {
            "attribute": "table",
            "op": "=",
            "value": "sbtest"
        }
    ]
}
```

# Performance

Perhaps the most significant factor affecting the performance of the cache is
whether the statements need to be parsed or not. By default, all statements are
parsed in order to exclude `SELECT` statements that use non-cacheable functions,
access non-cacheable variables or refer to system or user variables.

If it is known that no such statements are used or if it does not matter if the
results are cached, that safety measure can be turned off. To do that, add the
following line to the cache configuration:
```
[MyCache]
...
selects=assume_cacheable
```

With that configuration, the cache itself will not cause the statements to be
parsed.

But note that even with `assume_cacheable` configured, a rule referring
specifically to a _database_, _table_ or _column_ will still cause the
statement to be parsed.

For instance, a simple rule like
```
{
    "store": [
        {
            "attribute": "database",
            "op": "=",
            "value": "db1"
        }
    ]
}
```
cannot be fulfilled without parsing the statement.

If the rule is instead expressed using a regular expression
```
{
    "store": [
        {
            "attribute": "query",
            "op": "like",
            "value": "FROM db1\\..*"
        }
    ]
}
```
then the statement will again not be parsed.

However, even if regular expression matching performance wise is cheaper
than parsing, it still carries a cost. In the following is a table with numbers
giving a rough picture of the relative cost of different approaches.

In the table, _regexp match_ means that the cacheable statements
were picked out using a rule like
```
{
    "attribute": "query",
    "op": "like",
    "value": "FROM dbname"
}
```
while _exact match_ means that the cacheable statements were picked out using a
rule like
```
{
    "attribute": "database",
    "op": "=",
    "value": "dbname"
}
```
The exact match rule requires all statements to be parsed.

Note that the qps figures are only indicative.

| `selects`          | Rule           | qps |
| -------------------| ---------------|-----|
| `assume_cacheable` | none           | 100 |
| `assume_cacheable` | _regexp match_ |  98 |
| `assume_cacheable` | _exact match_  |  60 |
| `verify_cacheable` | none           |  60 |
| `verify_cacheable` | _regexp match_ |  58 |
| `verify_cacheable` | _exact match_  |  58 |

## Summary

For maximum performance:
* Arrange the situation so that `selects=assume_cacheable` can be
  configured, and use _no_ rules.
* If `selects=assume_cacheable` has been configured, use _only_
  regexp based rules.
* If `selects=verify_cacheable` has been configured non-regex based
  matching can be used.
