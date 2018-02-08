# Query Log All Filter

## Overview

The Query Log All (QLA) filter logs query content. Logs are written to a file in
CSV format. Log elements are configurable and include the time submitted and the
SQL statement text, among others.

## Configuration

A minimal configuration is below.
```
[MyLogFilter]
type=filter
module=qlafilter

[MyService]
type=service
router=readconnroute
servers=server1
user=myuser
passwd=mypasswd
filters=MyLogFilter
```

## Filter Options

The QLA filter accepts the following options.

 Option | Description
 -------| -----------
 ignorecase | Use case-insensitive matching
 case | Use case-sensitive matching
 extended | Use extended regular expression syntax (ERE)

To use multiple filter options, list them in a comma-separated list. If no
options are given, default will be used. Multiple options can be enabled
simultaneously.

```
options=case,extended
```

**Note**: older the version of the QLA filter in 0.7 of MariaDB MaxScale used
the `options` to define the location of the log files. This functionality is not
supported anymore and the `filebase` parameter should be used instead.

## Filter Parameters

The QLA filter has one mandatory parameter, `filebase`, and a number of optional
parameters. These were introduced in the 1.0 release of MariaDB MaxScale.

### `filebase`

The basename of the output file created for each session. A session index is
added to the filename for each written session file. For unified log files,
*.unified* is appended. This is a mandatory parameter.

```
filebase=/tmp/SqlQueryLog
```

The filebase may also be set as the filter option. If both option and parameter
are set, the parameter setting will be used and the filter option ignored.

### `match` and `exclude`

These optional parameters limit logging on a query level. The parameter values
are regular expressions which are matched against the SQL query text. Only SQL
statements that match the regular expression in *match* but do not match the
*exclude* expression are logged.

```
match=select.*from.*customer.*where
exclude=^insert
```

*match* is checked before *exclude*. If *match* is empty, all queries are
considered matching. If *exclude* is empty, no query is exluded. If both are
empty, all queries are logged.

### `user` and `source`

These optional parameters limit logging on a session level. If `user` is
defined, only the sessions with a matching client username are logged. If
`source` is defined, only sessions with a matching client source address are
logged.

```
user=john
source=127.0.0.1
```

### `log_type`

The type of log file to use. The default value is _session_.

|Value   | Description                    |
|--------|--------------------------------|
|session |Write to session-specific files |
|unified |Use one file for all sessions   |

```
log_type=session
```

If both logs are required, define `log_type=session,unified`.

### `log_data`

Type of data to log in the log files. The parameter value is a comma separated
list of the following elements. By default the _date_, _user_ and _query_
options are enabled.

| Value       | Description                                      |
| --------    |--------------------------------------------------|
| service     | Service name                                     |
| session     | Unique session id (ignored for session files)    |
| date        | Timestamp                                        |
| user        | User and hostname of client                      |
| reply_time  | Response time (ms until first reply from server) |
| query       | Query                                            |

```
log_data=date, user, query
```

If *reply_time* is enabled, the log entry is written when the first reply from
server is received. Otherwise, the entry is written when receiving query from
client.

### `flush`

Flush log files after every write. The default is false.

```
flush=true
```

### `append`

Append new entries to log files instead of overwriting them. The default is
false.

```
append=true
```

### `separator`

Default value is "," (a comma). Defines the separator string between elements of
a log entry. The value should be enclosed in quotes.

```
separator=" | "
```

### `newline_replacement`

Default value is " " (one space). SQL-queries may include line breaks, which, if
printed directly to the log, may break automatic parsing. This parameter defines
what should be written in the place of a newline sequence (\r, \n or \r\n). If
this is set as the empty string, then newlines are not replaced and printed as
is to the output. The value should be enclosed in quotes.

```
newline_replacement=" NL "
```

## Examples

### Example 1 - Query without primary key

Imagine you have observed an issue with a particular table and you want to
determine if there are queries that are accessing that table but not using the
primary key of the table. Let's assume the table name is PRODUCTS and the
primary key is called PRODUCT_ID. Add a filter with the following definition:

```
[ProductsSelectLogger]
type=filter
module=qlafilter
match=SELECT.*from.*PRODUCTS .*
exclude=WHERE.*PRODUCT_ID.*
filebase=/var/logs/qla/SelectProducts

[Product Service]
type=service
router=readconnrouter
servers=server1
user=myuser
passwd=mypasswd
filters=ProductsSelectLogger
```

The result of using this filter with the service used by the application would
be a log file of all select queries querying PRODUCTS without using the
PRODUCT_ID primary key in the predicates of the query. Executing `SELECT * FROM
PRODUCTS` would log the following into `/var/logs/qla/SelectProducts`:
```
07:12:56.324 7/01/2016, SELECT * FROM PRODUCTS
```
