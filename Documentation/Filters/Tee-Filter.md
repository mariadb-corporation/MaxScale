# Tee Filter

## Overview

The tee filter is a "plumbing" fitting in the MariaDB MaxScale filter toolkit.
It can be used in a filter pipeline of a service to make copies of requests from
the client and send the copies to another service within MariaDB MaxScale.

## Configuration

The configuration block for the TEE filter requires the minimal filter
parameters in its section within the MaxScale configuration file. The service to
send the duplicates to must be defined. Currently the tee filter does not
support multi-statements.

```
[DataMartFilter]
type=filter
module=tee
service=DataMart

[Data Service]
type=service
router=readconnrouter
servers=server1
user=myuser
passwd=mypasswd
filters=DataMartFilter
```

## Filter Options

The tee filter accepts the following options.

|Option    |Description                                 |
|----------|--------------------------------------------|
|ignorecase|Use case-insensitive matching               |
|case      |Use case-sensitive matching                 |
|extended  |Use extended regular expression syntax (ERE)|

To use multiple filter options, list them in a comma-separated list.

```
options=case,extended
```

## Filter Parameters

The tee filter requires a mandatory parameter to define the service to replicate
statements to and accepts a number of optional parameters.

### Match

An optional parameter used to limit the queries that will be replicated by the
tee filter. The parameter value is a regular expression that is used to match
against the SQL text. Only SQL statements that matches the text passed as the
value of this parameter will be sent to the service defined in the filter
section.

```
match=insert.*into.*order*
```

All regular expressions are evaluated with the option to ignore the case of the
text, therefore a match option of select will match both insert, INSERT and any
form of the word with upper or lowercase characters.

### Exclude

An optional parameter used to limit the queries that will be replicated by the
tee filter. The parameter value is a regular expression that is used to match
against the SQL text. SQL statements that match the text passed as the value of
this parameter will be excluded from the replication stream.

```
exclude=select
```

All regular expressions are evaluated with the option to ignore the case of the
text, therefore an exclude option of select will exclude statements that contain
both select, SELECT or any form of the word with upper or lowercase characters.

### Source

The optional source parameter defines an address that is used to match against
the address from which the client connection to MariaDB MaxScale originates.
Only sessions that originate from this address will be replicated.

```
source=127.0.0.1
```

### User

The optional user parameter defines a user name that is used to match against
the user from which the client connection to MariaDB MaxScale originates. Only
sessions that are connected using this username are replicated.

```
user=john
```

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
passwd=6628C50E07CCE1F0392EDEEB9D1203F3
filters=ReplicateOrders

[ReplicateOrders]
type=filter
module=tee
service=DataMart
match=insert[ 	]*into[ 	]*orders

[DataMart]
type=service
router=readconnroute
servers=datamartserver
user=massi
passwd=6628C50E07CCE1F0392EDEEB9D1203F3
filters=QLA_DataMart

[QLA_DataMart]
type=filter
module=qlafilter
options=/var/log/DataMart/InsertsLog

[Orders Listener]
type=listener
service=Orders
protocol=MySQLClient
port=4011

[DataMart Listener]
type=listener
service=DataMart
protocol=MySQLClient
port=4012
```
