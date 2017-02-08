# Insert Stream Filter

This filter was introduced in MariaDB MaxScale 2.1.

## Overview
The _insertstream_ filter converts bulk inserts into CSV data streams that are
consumed by the backend server via the LOAD DATA LOCAL INFILE mechanism. This
leverages the speed advantage of LOAD DATA LOCAL INFILE over regular inserts
while also reducing the overall network traffic by condensing the inserted
values into CSV.

**Note**: This is an experimental filter module

## Filter Parameters

This filter has no parameters.

## Details of Operation

The filter translates all INSERT statements done inside an explicit transaction
into LOAD DATA LOCAL INFILE streams. The file name used in the request will
always be _maxscale.data_.

The following example is translated into a LOAD DATA LOCAL INFILE request
followed by two CSV rows.

```
BEGIN;
INSERT INTO test.t1 VALUES (1, "hello"), (2, "world");
COMMIT;
```

Multiple inserts to the same table are combined into a single stream. This
allows for efficient bulk loading with simple insert statements.

The following example will use only one LOAD DATA LOCAL INFILE request followed
by four CSV rows.

```
BEGIN;
INSERT INTO test.t1 VALUES (1, "hello"), (2, "world");
INSERT INTO test.t1 VALUES (3, "foo"), (4, "bar");
COMMIT;
```

Non-INSERT statements executed inside the transaction will close the streaming
of the data. Avoid interleaving SELECT statements with INSERT statements inside
transactions.

The following example has to use two LOAD DATA LOCAL INFILE requests, each
followed by two CSV rows.

**Note:** Avoid doing this!

```
BEGIN;
INSERT INTO test.t1 VALUES (1, "hello"), (2, "world");
SELECT * FROM test.t1;
INSERT INTO test.t1 VALUES (3, "foo"), (4, "bar");
COMMIT;
```

### Estimating Network Bandwidth Reduction

The more inserts that are streamed, the more efficient this filter is. The
saving in network bandwidth in bytes can be estimated with the following
formula:

```
((20 + t) * n) + (n * (m * 2)) - 108 - t = x

n = Number of INSERT statements
m = Number of values in each insert statement
t = Length of table name
x = Number of bytes saved
```

Positive values indicate savings in network bandwidth usage.

## Example Configuration

The filter has no parameters so it is extremely simple to configure. The
following example shows the required filter configuration.

```
[Insert-Stream]
type=filter
module=insertstream
```
