# BinlogFilter

This filter was introduced in MariaDB MaxScale 2.3.

## Overview
The BinlogFilter filter is capable of filtering and replacing the binlog events
sent from BinlogRouter to slave servers.

This filter, once configured with a database or table name, inspects
the binlog events being delivered from Binlog Server to connected slave servers.
If a match for db/table occurs in binlog events such as
TABLE_MAP and QUERY_EVENT, all the following ones are replaced
with small (35 bytes) RAND_EVENT events.
The replacement continues until a COMMIT, new TABLE_MAP event or
QUERY_EVENT is seen.

The result is that the slave server is not getting any real data about
DDLs or DMLS related to the configured db/table.

Assuming a simple transaction with an INSERT is being delivered with
ROW based replication, the slave server sees:

 - GTID (BEGIN)
 - RAND_EVENT (instead of TABLE_MAP)
 - RAND_EVENT (instead of WRITE_ROWS)
 - XID (COMMIT)

This replacement also saves network bandwidth as a 3MB BLOB
update is not sent by MaxScale Binlog Server to any slave,
just a 35 bytes RAND_EVENT is sent istead.

**Note**:

 - In case of large event transmission, i.e. 33MBytes, all the 3 packets
 (16 + 16 + 1 MB) that form the large event are replaced by RAND_EVENT
 - The packet sequence is always kept during the replacement.
 - No configuration is needed in the slave servers.
 - The filter can work with both ROW and STATEMENT based replication.

## Configuration

The BinlogFilter configuration is very easy, and in its first implementation
it has no mandatory parameters.

Optional parameters are:

#### `filter_events`
Enables the binlog filtering, default is Off

```
filter_events=On
```

#### `skip_db`
Specifies the database name to skip.
Empty setting, skip_db=, or no setting
means that all database names are allowed.
The default value is empty value.

```
skip_db=orders_t1
```

#### `skip_table`

Specifies the table name to skip.
Empty setting, skip_table=, or no setting
means that all tables names are allowed.
The default value is empty value.

```
skip_db=cat_obj
```

## Example Configuration

Here is an example of BinlogFilter:

All binlog events belonging to database `
test` and table `tbl1` are replaced.

```
[BinlogFilter]
type=filter
module=binlogfilter
filter_events=On
skip_db=test
skip_table=tbl1

[BinlogServer]
type=service
router=binlogrouter
server-id=33,
filters=BinlogFilter
```
