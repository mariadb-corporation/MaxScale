#Cache

## Overview
The cache filter is capable of caching the result of SELECTs, so that subsequent identical
SELECTs are served directly by MaxScale, without being routed to any server.

## Configuration

The cache filter is straightforward to configure and simple to add to any
existing service.

```
[Cache]
type=filter
module=cache
ttl=5
storage=...
storage_args=...

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

The cache filter has one mandatory parameter - `storage` - and a few
optional ones.

#### `storage`

The name of the module that provides the storage for the cache. That
module will be loaded and provided with the value of `storage_args` as
argument. For instance:
```
storage=storage_rocksdb
```

#### `storage_args`

A comma separated list of arguments to be provided to the storage module,
specified in `storage`, when it is loaded. Note that the needed arguments
depend upon the specific module. For instance,
```
storage_args=path=/usr/maxscale/cache/rocksdb
```

#### `allowed_references`

Specifies whether any or only fully qualified references are allowed in
queries stored to the cache.
```
allowed_references=[fully-qualified|any]
```
The default is `fully-qualified`, which means that only queries where
the database name is included in the table name are subject to caching.
```
select col from db.tbl;
```
If `any` is specified, then also queries where the table name is not
fully qualified are subject to caching.
```
select col from tbl;
```
Care should be excersized before this setting is changed, because, for
instance, the following is likely to produce unexpected results.
```
use db1;
select col from tbl;
...
use db2;
select col from tbl;
```
The setting can be changed to `any`, provided fully qualified names
are always used or if the names of tables in different databases are
different.

#### `maximum_resultset_size`

Specifies the maximum size a resultset can have, measured in kibibytes,
in order to be stored in the cache. A resultset larger than this, will
not be stored.
```
maximum_resultset_size=64
```
The default value is TBD.

#### `ttl`

_Time to live_; the amount of time - in seconds - the cached result is used
before it is refreshed from the server.

If nothing is specified, the default _ttl_ value is 10.

```
ttl=60
```

#Storage

## Storage RocksDB
