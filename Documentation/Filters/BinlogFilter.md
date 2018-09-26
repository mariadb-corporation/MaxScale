# Binlog Filter

This filter was introduced in MariaDB MaxScale 2.3.0.

## Overview

The `binlogfilter` can be combined with a `binlogrouter` service to selectively
replicate the binary log events to slave servers.

The filter uses two parameters, `match` and `exclude`, to decide which events
are replicated. If a binlog event does not match or is excluded, the event is
replaced with an empty data event. The empty event is always 35 bytes which
translates to a space reduction in most cases.

The filter works with both row based and statement based replication but we
recommend using row based replication with the binlogfilter. This guarantees
that there are no ambiguities in the event filtering.

## Configuration

Both the `match` and `exclude` parameters are optional. If neither of them is
defined, the filter does nothing and all events are replicated.

The two parameters are matched against the database and table name concatenated
with a period.  For example, the string the patterns are matched against for the
database `test` and table `t1` is `test.t1`.

For statement based replication, the pattern is matched against all the tables
in the statements. If any of the tables matches the `match` pattern, the event
is replicated. If any of the tables matches the `exclude` pattern, the event is
not replicated.


### `match`

A [PCRE2 regular expression](../Getting-Started/Configuration-Guide.md#regular-expressions)
that is matched against the database and table name. If the pattern matches, the
event is replicated to the slave. If no `match` parameter is defined, all events
are considered to match.

### `exclude`

A [PCRE2 regular expression](../Getting-Started/Configuration-Guide.md#regular-expressions)
that is matched against the database and table name. If the pattern matches, the
event is excluded and is not replicated to the slave. If no `exclude` pattern is
defined, the event filtering is controlled completely by the `match` parameter.

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
[binlogrouter documentation](../Routers/Binlogrouter.md) and the
[binlogrouter tutorial](../Tutorials/Replication-Proxy-Binlog-Router-Tutorial.md).
