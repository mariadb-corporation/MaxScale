# Maxrows

This filter was introduced in MariaDB MaxScale 2.1.

[TOC]

## Overview
The Maxrows filter is capable of restricting the amount of rows that a SELECT,
 a prepared statement or stored procedure could return to the client application.

If a resultset from a backend server has more rows than the configured limit
or the resultset size exceeds the configured size,
 an empty result will be  sent to the client.

## Configuration

The Maxrows filter is easy to configure and to add to any existing service.

```
[MaxRows]
type=filter
module=maxrows

[MaxRows-Routing-Service]
type=service
...
filters=MaxRows
```

### Filter Parameters

The Maxrows filter has no mandatory parameters.
Optional parameters are:

#### `max_resultset_rows`

- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: (no limit)

Specifies the maximum number of rows a resultset can have in order to be
returned to the user.

If a resultset is larger than this an empty result will be sent instead.

```
max_resultset_rows=1000
```

#### `max_resultset_size`

- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `64Ki`

Specifies the maximum size a resultset can have in order
to be sent to the client. A resultset larger than this, will
not be sent: an empty resultset will be sent instead.
```
max_resultset_size=128Ki
```

#### `max_resultset_return`

- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `empty`, `error`, `ok`
- **Default**: `empty`

Specifies what the filter sends to the client when the
rows or size limit is hit, possible values:

- an empty result set
- an error packet with input SQL
- an OK packet

Example output with ERR packet:

```
MariaDB [(test)]> select * from test.t4;
ERROR 1415 (0A000): Row limit/size exceeded for query: select * from test.t4
```

#### `debug`

- **Type**: number
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `0`

An integer value, using which the level of debug logging made by the Maxrows
filter can be controlled. The value is actually a bitfield with different bits
denoting different logging.

   * ` 0` (`0b00000`) No logging is made.
   * ` 1` (`0b00001`) A decision to handle data form server is logged.
   * ` 2` (`0b00010`) Reached max_resultset_rows or max_resultset_size is logged.

To log everything, give `debug` a value of `3`.

```
debug=2
```

## Example Configuration

Here is an example of filter configuration where the maximum number of returned
rows is 10000 and maximum allowed resultset size is 256KB

```
[MaxRows]
type=filter
module=maxrows
max_resultset_rows=10000
max_resultset_size=256000
```
