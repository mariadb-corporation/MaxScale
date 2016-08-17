# Consistent Critical Read Filter

## Overview

The Consistent Critical Read (CCR) filter allows consistent critical reads to be
done through MaxScale while still allowing scaleout of non-critical reads.

When the filter detects a statement that would modify the database, it attaches a
routing hint to all following statements. This routing hint guides the routing
module to route the statement to the master server where data is guaranteed to be
in a up-to-date state.

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

Enabling this parameter in combination with the _count_ parameter causes both the
time window and number of queries to be inspected. If either of the two
conditions are met, the query is re-routed to the master.

### `count`

The number of SQL statements to route to master after detecting a data modifying
SQL statement. This feature is disabled by default.

After processing a data modifying SQL statement, a counter is set to the value of
_count_ and all statements are routed to the master. Each executed statement
after a data modifying SQL statement cause the counter to be decremented. Once
the counter reaches zero, the statements are routed normally. If a new data
modifying SQL statement is processed, the counter is reset to the value of
_count_.

### `match`

An optional parameter that can be used to control which statements trigger the
statement re-routing. The parameter value is a regular expression that is used to
match against the SQL text. Only non-SELECT statements are inspected.

### `ignore`

An optional parameter that can be used to control which statements don't trigger
the statement re-routing. This does the opposite of the _match_ parameter. The
parameter value is a regular expression that is used to match against the SQL
text. Only non-SELECT statements are inspected.
