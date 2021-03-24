# Binlog Filter

This filter was introduced in MariaDB MaxScale 2.3.0.

[TOC]

## Overview

The `binlogfilter` can be combined with a `binlogrouter` service to selectively
replicate the binary log events to slave servers.

The filter uses two parameters, *match* and *exclude*, to decide which events
are replicated. If a binlog event does not match or is excluded, the event is
replaced with an empty data event. The empty event is always 35 bytes which
translates to a space reduction in most cases.

The filter works with both row based and statement based replication but we
recommend using row based replication with the binlogfilter. This guarantees
that there are no ambiguities in the event filtering.

## Configuration

### `match` and `exclude`

Both the *match* and *exclude* parameters are optional and work mostly as other
[typical regular expression parameters](../Getting-Started/Configuration-Guide.md#standard-regular-expression-settings-for-filters).
If neither of them is defined, the filter does nothing and all events are replicated. This
filter does not accept regular expression options as a separate parameter, such settings
must be defined in the patterns themselves. See the
[PCRE2 api documentation](https://www.pcre.org/current/doc/html/pcre2api.html#SEC20) for
more information.

The two parameters are matched against the database and table name concatenated
with a period.  For example, the string the patterns are matched against for the
database `test` and table `t1` is `test.t1`.

For statement based replication, the pattern is matched against all the tables
in the statements. If any of the tables matches the *match* pattern, the event
is replicated. If any of the tables matches the *exclude* pattern, the event is
not replicated.

### `rewrite_src` and `rewrite_dest`

These two parameters control the statement rewriting of the binlogfilter. The
`rewrite_src` parameter is a PCRE2 regular expression that is matched against
the default database and the SQL of statement based replication events (query
events). `rewrite_dest` is the replacement string which supports the normal
PCRE2 backreferences (e.g the first capture group is `$1`, the second is `$2`,
etc.).

Both `rewrite_src` and `rewrite_dest` must be defined to enable statement rewriting.

When statement rewriting is enabled
[GTID-based replication](https://mariadb.com/kb/en/library/gtid/#setting-up-a-new-slave-server-with-global-transaction-id)
must be used. The filter will disallow replication for all slaves that attempt
to replicate with traditional file-and-position based replication.

The replacement is done both on the default database as well as the SQL
statement in the query event. This means that great care must be taken when
defining the rewriting rules. To prevent accidental modification of the SQL into
a form that is no longer valid, use database and table names that never occur in
the inserted data and is never used as a constant value.

## Example Configuration

With the following configuration, only events belonging to database `customers`
are replicated. In addition to this, events for the table `orders` are excluded
and thus are not replicated.

```
[BinlogFilter]
type=filter
module=binlogfilter
match=/customers[.]/
exclude=/[.]orders/

[BinlogServer]
type=service
router=binlogrouter
server_id=33
filters=BinlogFilter

[BinlogListener]
type=listener
service=BinlogServer
protocol=MySQLClient
port=4000
```

For more information about the binlogrouter and how to use it, refer to the
[binlogrouter documentation](../Routers/Binlogrouter.md).
