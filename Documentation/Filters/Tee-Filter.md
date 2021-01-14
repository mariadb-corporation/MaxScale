# Tee Filter

## Overview

The tee filter is a "plumbing" fitting in the MariaDB MaxScale filter toolkit.
It can be used in a filter pipeline of a service to make copies of requests from
the client and send the copies to another service within MariaDB MaxScale.

**Please Note:** Starting with MaxScale 2.2.0, any client that connects to a
  service which uses a tee filter will require a grant for the loopback address,
  i.e. `127.0.0.1`.

## Configuration

The configuration block for the TEE filter requires the minimal filter
parameters in its section within the MaxScale configuration file. The service to
send the duplicates to must be defined.

```
[DataMartFilter]
type=filter
module=tee
target=DataMart

[Data-Service]
type=service
router=readconnroute
servers=server1
user=myuser
password=mypasswd
filters=DataMartFilter
```

## Filter Parameters

The tee filter requires a mandatory parameter to define the service to replicate
statements to and accepts a number of optional parameters.

### `target`

The target where the filter will duplicate all queries. The target can be either
a service or a server. The duplicate connection that is created to this target
will be referred to as the "branch target" in this document.

### `service`

The service where the filter will duplicate all queries. This parameter is
deprecated in favor of the `target` parameter and will be removed in a future
release. Both `target` and `service` cannot be defined.

### `match`, `exclude` and `options`

These [regular expression settings](../Getting-Started/Configuration-Guide.md#standard-regular-expression-settings-for-filters)
limit the queries replicated by the tee filter.

```
match=/insert.*into.*order*/
exclude=/select.*from.*t1/
options=case,extended
```

### `source`

The optional source parameter defines an address that is used to match against
the address from which the client connection to MariaDB MaxScale originates.
Only sessions that originate from this address will be replicated.

```
source=127.0.0.1
```

### `user`

The optional user parameter defines a user name that is used to match against
the user from which the client connection to MariaDB MaxScale originates. Only
sessions that are connected using this username are replicated.

```
user=john
```

## Limitations

- All statements that are executed on the branch target are done in an
  asynchronous manner. This means that when the client receives the response
  there is no guarantee that the statement has completed on the branch target.

- Any errors on the branch target will cause the connection to it to be
  closed. If `target` is a service, it is up to the router to decide whether the
  connection is closed. For direct connections to servers, any network errors
  cause the connection to be closed. When the connection is closed, no new
  queries will be routed to the branch target.

## Module commands

Read [Module Commands](../Reference/Module-Commands.md) documentation for
details about module commands.

The tee filter supports the following module commands.

### `tee disable [FILTER]`

This commmad disables a tee filter instance. A disabled tee filter will not send
any queries to the target service.

### `tee enable [FILTER]`

Enable a disabled tee filter. This resumes the sending of queries to the target
service.

## Examples

### Example 1 - Replicate all inserts into the orders table

Assume an order processing system that has a table called orders. You also have
another database server, the datamart server, that requires all inserts into
orders to be replicated to it. Deletes and updates are not, however, required.

Set up a service in MariaDB MaxScale, called Orders, to communicate with the
order processing system with the tee filter applied to it. Also set up a service
to talk to the datamart server, using the DataMart service. The tee filter would
have as it’s service entry the DataMart service, by adding a match parameter of
"insert into orders" would then result in all requests being sent to the order
processing system, and insert statements that include the orders table being
additionally sent to the datamart server.

```
[Orders]
type=service
router=readconnroute
servers=server1, server2, server3, server4
user=massi
password=6628C50E07CCE1F0392EDEEB9D1203F3
filters=ReplicateOrders

[ReplicateOrders]
type=filter
module=tee
target=DataMart
match=insert[ 	]*into[ 	]*orders

[DataMart]
type=service
router=readconnroute
servers=datamartserver
user=massi
password=6628C50E07CCE1F0392EDEEB9D1203F3
filters=QLA-DataMart

[QLA-DataMart]
type=filter
module=qlafilter
options=/var/log/DataMart/InsertsLog

[Orders-Listener]
type=listener
target=Orders
protocol=MariaDBClient
port=4011

[DataMart-Listener]
type=listener
target=DataMart
protocol=MariaDBClient
port=4012
```
