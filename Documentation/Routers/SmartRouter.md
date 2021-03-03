# SmartRouter

[TOC]

## Overview

SmartRouter is the query router of the SmartQuery framework. Based on the type
of the query, each query is routed to the server or cluster that can best
handle it.

For workloads where both transactional and analytical queries are needed,
SmartRouter unites the Transactional (OLTP) and Analytical (OLAP) workloads into
a single entry point in MaxScale. This allows a MaxScale client to freely mix
transactional and analytical queries using the same connection. This is known
as Hybrid Transactional and Analytical Processing, HTAP.

## Configuration

SmartRouter is configured as a service that either routes to other MaxScale
routers or plain servers. Although one can configure SmartRouter to use a plain
server directly, we refer to the configured "servers" as clusters.

For details about the standard service parameters, refer to the
[Configuration Guide](../Getting-Started/Configuration-Guide.md).

### `master`

One of the clusters must be designated as the **`master`**. All writes go to the
master cluster, which for all practical purposes should be a master-slave
ReadWriteSplit. This document does not go into details about setting up
master-slave clusters, but suffice to say, that when setting up the ColumnStore
servers they should be configured to be slaves of a MariaDB server running an
InnoDB engine.
The ReadWriteSplit [documentation](ReadWriteSplit.md) has more on master-slave setup.

#### Example

Suppose we have a Transactional service like
```
[RWS-Row]
type=service
router=readwritesplit
servers = row_server_1, row_server_2, ...
```
for which we have defined the listener
```
[RWS-Row-Listener]
type=listener
service=RWS-Row
socket=/tmp/rws-row.sock
```
That is, that service can be accessed using the socket `/tmp/rws-row.sock`.

The Analytical service could look like this
```
[RWS-Column]
type = service
router = readwritesplit
servers = column_server_1, column_server_2, ...

[RWS-Column-Listener]
type = listener
service = RWS-Column
socket = /tmp/rws-col.sock
```

Then we can define the SmartQuery service as follows
```
[SmartQuery]
type = service
router = smartrouter
targets = RWS-Row, RWS-Column
master = RWS-Row

[SmartQuery-Listener]
type = listener
service = SmartQuery
port = <port>
```
Note that the SmartQuery listener listens on a port, while the Row and Column
service listeners listen on Unix domain sockets. The reason is that there is a
significant performance benefit when SmartRouter accesses the services over a
Unix domain socket compared to accessing them over a TCP/IP socket.

A complete configuration example can be found at the end of this document.

## Cluster selection - how queries are routed

SmartRouter keeps track of the performance, or the execution time, of queries to
the clusters. Measurements are stored with the canonical of a query as the key.
The canonical of a query is the sql with all user-defined constants replaced with
question marks. When SmartRouter sees a read-query whose canonical has not been
seen before, it will send the query to all clusters. The first response from a
cluster will designate that cluster as the best one for that canonical. Also,
when the first response is received, the other queries are cancelled. The
response is sent to the client once all clusters have responded to the query
or the cancel.

There is obviously overhead when a new canonical is seen. This means that
queries after a MaxScale start will be slightly slower than normal. The
execution time of a query depends on the database engine, and on the contents
of the tables being queried. As a result, MaxScale will periodically re-measure
queries.

The performance behavior of queries under dynamic conditions, and their effect
on different storage engines is being studied at MariaDB. As we learn more, we
will be able to better categorize queries and move that knowledge into
SmartRouter.

## Limitations

* `LOAD DATA LOCAL INFILE` is not supported.
* The performance data is not persisted. The measurements will be performed
anew after each startup.

## Complete configuration example
```
[maxscale]

[row_server_1]
type = server
address = <ip>
port = <port>

[row_server_2]
type = server
address = <ip>
port = <port>

[Row-Monitor]
type = monitor
module = mariadbmon
servers = row_server_1, row_server_2
user = <user>
password = <password>
monitor_interval = 2000ms

[column_server_1]
type = server
address = <ip>
port = <port>

[Column-Monitor]
type = monitor
module = csmon
servers = column_server_1
user = <user>
password = <password>
monitor_interval = 2000ms

# Row Read write split
[RWS-Row]
type = service
router = readwritesplit
servers = row_server_1, row_server_2
user = <user>
password = <password>

[RWS-Row-Listener]
type = listener
service = RWS-Row
socket = /tmp/rws-row.sock

# Columnstore Read write split
[RWS-Column]
type = service
router = readwritesplit
servers = column_server_1
user = <user>
password = <password>

[RWS-Column-Listener]
type = listener
service = RWS-Column
socket = /tmp/rws-col.sock

# Smart Query router
[SmartQuery]
type = service
router = smartrouter
targets = RWS-Row, RWS-Column
master = RWS-Row
user = <user>
password = <password>

[SmartQuery-Listener]
type = listener
service = SmartQuery
port = <port>
```
