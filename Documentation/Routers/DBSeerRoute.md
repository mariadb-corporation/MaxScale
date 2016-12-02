# DBSeerRoute

This document provides an overview of the **dbseerroute** router module and its intended use case scenarios. It also displays all router configuration parameters with their descriptions.

## Overview

The dbseerroute router is a modified version of the readconnroute router. The dbseerroute router provides simple and lightweight load balancing across a set of servers just like readconnroute router. The router can also be configured to balance connections based on a weighting parameter defined in the server's section. The only difference is that the router logs all committed transactions with necessary information, such as timestamp, server, SQL statements, latency, etc., which can be used later for transaction performance analysis.

**NOTE: dbseerroute requires `autocommit` option from MySQL/MariaDB to be OFF as it detects 'rollback' or 'commit' statements to distinguish different transactions.**

**NOTE: the dbseerroute router shares same optional paramters with the readconnroute router.**

## Configuration

DBSeerRoute router-specific settings are specified in the configuration file of MaxScale in its specific section. The section can be freely named but the name is used later as a reference from listener section.

For more details about the standard service parameters, refer to the [Configuration Guide](../Getting-Started/Configuration-Guide.md).

## Mandatory parameters

**`type`** specifies the type of service. For dbseerroute module the type is `service`:

    type=service

**`router`** specifies the router module to be used. For dbseerroute the value is `dbseerroute`:

    router=dbseerroute

**`servers`** provides a list of servers, which the router will connect to:

    servers=server1,server2,server3

**NOTE: Each server on the list must have its own section in the configuration file where it is defined.**

**`user`** is the username the router session uses for accessing backends in order to load the content of the `mysql.user` table (and `mysql.db` and database names as well) and optionally for creating, and using `maxscale_schema.replication_heartbeat` table.

**`passwd`** specifies corresponding password for the user. Syntax for user and passwd is:

```
user=<username>
passwd=<password>
```

**`log_filename`** is the name of the file, which dbseerroute will write transaction logs to:

    log_filename=/tmp/transaction.log

**`log_delimiter`** defines a delimiter that is used to distinguish columns in the log:

    log_delimiter=:::

**`query_delimiter`** defines a delimiter that is used to distinguish different SQL statements in a transaction.

    query_delimiter=@@@

**`named_pipe`** is the path to a named pipe, which dbseerroute uses to communicate with the DBSeer middleware.

	named_pipe=/tmp/dbseerroute
	

## Router Options

**`router_options`** can contain a list of valid server roles. These roles are used as the valid types of servers the router will form connections to when new sessions are created.
```
	router_options=slave
```
Here is a list of all possible values for the `router_options`.

Role|Description
------|---------
master|A server assigned as a master by one of MariaDB MaxScale monitors. Depending on the monitor implementation, this could be a master server of a Master-Slave replication cluster or a Write-Master of a Galera cluster.
slave|A server assigned as a slave of a master.
synced| A Galera cluster node which is in a synced state with the cluster.
ndb|A MySQL Replication Cluster node
running|A server that is up and running. All servers that MariaDB MaxScale can connect to are labeled as running.

If no `router_options` parameter is configured in the service definition, the router will use the default value of `running`. This means that it will load balance connections across all running servers defined in the `servers` parameter of the service.

When a connection is being created and the candidate server is being chosen, the
list of servers is processed in from first entry to last. This means that if two
servers with equal weight and status are found, the one that's listed first in
the _servers_ parameter for the service is chosen.

## Limitations

For a list of dbseerroute/readconnroute limitations, please read the [Limitations](../About/Limitations.md) document.

## Examples

For examples related to readconnroute, take a look at [readconnroute](./ReadConnRoute.md) documentation.

### Example 1 - Log transactions for performance analysis

You want to log every transaction with its SQL statements and latency for future transaction performance analysis.

Add a dbseerroute router with the following configuration:

```
[maxscale]
threads=1

[Performance Log Router]
type=service
router=dbseerroute
servers=server1
user=root
passwd=
enable_root_user=true
log_filename=/var/logs/maxscale/perf.log
log_delimiter=:::
query_delimiter=@@@
named_pipe=/tmp/dbseerroute

[Read Connection Listener]
type=listener
service=Performance Log Router
protocol=MySQLClient
port=3600
socket=/home/dyoon/maxscale/readconn.sock
```

The following is an example log that is generated from the dbseerroute router with the above configuration:

```
1453751768:::server1:::localhost:::3:::UPDATE WAREHOUSE SET W_YTD = W_YTD + 900.86  WHERE W_ID = 2 @@@SELECT W_STREET_1, W_STREET_2, W_CITY, W_STATE, W_ZIP, W_NAME FROM WAREHOUSE WHERE W_ID = 2@@@UPDATE DISTRICT SET D_YTD = D_YTD + 900.86 WHERE D_W_ID = 2 AND D_ID = 5@@@SELECT D_STREET_1, D_STREET_2, D_CITY, D_STATE, D_ZIP, D_NAME FROM DISTRICT WHERE D_W_ID = 2 AND D_ID = 5@@@SELECT C_FIRST, C_MIDDLE, C_ID, C_STREET_1, C_STREET_2, C_CITY, C_STATE, C_ZIP, C_PHONE, C_CREDIT, C_CREDIT_LIM, C_DISCOUNT, C_BALANCE, C_YTD_PAYMENT, C_PAYMENT_CNT, C_SINCE FROM CUSTOMER WHERE C_W_ID = 2 AND C_D_ID = 5 AND C_LAST = 'CALLYCALLYATION' ORDER BY C_FIRST@@@UPDATE CUSTOMER SET C_BALANCE = -90026.89, C_YTD_PAYMENT = 93507.06, C_PAYMENT_CNT = 38 WHERE C_W_ID = 2 AND C_D_ID = 5 AND C_ID = 779@@@INSERT INTO HISTORY (H_C_D_ID, H_C_W_ID, H_C_ID, H_D_ID, H_W_ID, H_DATE, H_AMOUNT, H_DATA)  VALUES (5,2,779,5,2,'2016-01-25 14:56:08',900.86,'gqfla    adopdon')
1453751768:::server1:::localhost:::5:::UPDATE WAREHOUSE SET W_YTD = W_YTD + 3679.75  WHERE W_ID = 2 @@@SELECT W_STREET_1, W_STREET_2, W_CITY, W_STATE, W_ZIP, W_NAME FROM WAREHOUSE WHERE W_ID = 2@@@UPDATE DISTRICT SET D_YTD = D_YTD + 3679.75 WHERE D_W_ID = 2 AND D_ID = 1@@@SELECT D_STREET_1, D_STREET_2, D_CITY, D_STATE, D_ZIP, D_NAME FROM DISTRICT WHERE D_W_ID = 2 AND D_ID = 1@@@SELECT C_FIRST, C_MIDDLE, C_LAST, C_STREET_1, C_STREET_2, C_CITY, C_STATE, C_ZIP, C_PHONE, C_CREDIT, C_CREDIT_LIM, C_DISCOUNT, C_BALANCE, C_YTD_PAYMENT, C_PAYMENT_CNT, C_SINCE FROM CUSTOMER WHERE C_W_ID = 2 AND C_D_ID = 1 AND C_ID = 203@@@UPDATE CUSTOMER SET C_BALANCE = 1600482.5, C_YTD_PAYMENT = 1192789.8, C_PAYMENT_CNT = 485 WHERE C_W_ID = 2 AND C_D_ID = 1 AND C_ID = 203@@@INSERT INTO HISTORY (H_C_D_ID, H_C_W_ID, H_C_ID, H_D_ID, H_W_ID, H_DATE, H_AMOUNT, H_DATA)  VALUES (1,2,203,1,2,'2016-01-25 14:56:08',3679.75,'gqfla    uquslfu')
...
```

Note that 3 and 5 are latencies of each transaction in milliseconds.
