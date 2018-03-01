# Consistent Critical Read Filter

This filter was introduced in MariaDB MaxScale 2.1.

## Overview

The Consistent Critical Read (CCR) filter allows consistent critical reads to be
done through MaxScale while still allowing scaleout of non-critical reads.

When the filter detects a statement that would modify the database, it attaches
a routing hint to all following statements. This routing hint guides the routing
module to route the statement to the master server where data is guaranteed to
be in an up-to-date state.

## Filter Options

The CCR filter accepts the following options.

|Option     |Description                                 |
|-----------|--------------------------------------------|
|ignorecase |Use case-insensitive matching (default)     |
|case       |Use case-sensitive matching                 |
|extended   |Use extended regular expression syntax (ERE)|

To use multiple filter options, list them in a comma-separated list.

```
options=case,extended
```

## Filter Parameters

The CCR filter has no mandatory parameters.

### `time`

The time window in seconds during which queries are routed to the master. The
default value for this parameter is 60 seconds.

When a data modifying SQL statement is processed, a timer is set to the value of
_time_. Once the timer has elapsed, all statements are routed normally. If a new
data modifying SQL statement is processed within the time window, the timer is
reset to the value of _time_.

Enabling this parameter in combination with the _count_ parameter causes both
the time window and number of queries to be inspected. If either of the two
conditions are met, the query is re-routed to the master.

### `count`

The number of SQL statements to route to master after detecting a data modifying
SQL statement. This feature is disabled by default.

After processing a data modifying SQL statement, a counter is set to the value
of _count_ and all statements are routed to the master. Each executed statement
after a data modifying SQL statement cause the counter to be decremented. Once
the counter reaches zero, the statements are routed normally. If a new data
modifying SQL statement is processed, the counter is reset to the value of
_count_.

### `match`

An optional parameter that can be used to control which statements trigger the
statement re-routing. The parameter value is a regular expression that is used
to match against the SQL text. Only non-SELECT statements are inspected.

```
match=.*INSERT.*
```

### `ignore`

An optional parameter that can be used to control which statements don't trigger
the statement re-routing. This does the opposite of the _match_ parameter. The
parameter value is a regular expression that is used to match against the SQL
text. Only non-SELECT statements are inspected.

```
ignore=.*UPDATE.*
```

## Example Configuration

Here is a minimal filter configuration for the CCRFilter which should solve most
problems with critical reads after writes.

```
[CCRFilter]
type=filter
module=ccrfilter
time=5
```

This configuration will force all read queries after a write to the master for 5
seconds, preventing read scaling until the modifications have been replicated to
the slaves.

For best performance, the value of _time_ should be slightly greater than the
actual replication lag between the master and its slaves. If the number of
critical read statements is known, the _count_ parameter could be used to
control the number reads that are sent to the master.
