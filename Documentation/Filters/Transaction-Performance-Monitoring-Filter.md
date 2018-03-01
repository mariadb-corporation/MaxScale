# Transaction Performance Monitoring Filter

## Overview

The Transaction Performance Monitoring (TPM) filter is a filter module for MaxScale
that monitors every SQL statement that passes through the filter.
The filter groups a series of SQL statements into a transaction by detecting
'commit' or 'rollback' statements. It logs all committed transactions with necessary
information, such as timestamp, client, SQL statements, latency, etc., which
can be used later for transaction performance analysis.

## Configuration

The configuration block for the TPM filter requires the minimal filter
options in it's section within the maxscale.cnf file, stored in /etc/maxscale.cnf.

```
[MyLogFilter]
type=filter
module=tpmfilter

[MyService]
type=service
router=readconnrouter
servers=server1
user=myuser
passwd=mypasswd
filters=MyLogFilter
```

## Filter Options

The TPM filter does not support any filter options currently.

## Filter Parameters

The TPM filter accepts a number of optional parameters.

### Filename

The name of the output file created for performance logging.
The default filename is **tpm.log**.

```
filename=/tmp/SqlQueryLog
```

### Source

The  optional  `source`  parameter  defines  an  address  that  is  used
to  match  against  the  address  from  which  the  client  connection
to  MaxScale  originates.  Only  sessions  that  originate  from  this
address  will  be  logged.

```
source=127.0.0.1
```

### User

The  optional  `user`  parameter  defines  a  user  name  that  is  used
to  match  against  the  user  from  which  the  client  connection  to
MaxScale  originates.  Only  sessions  that  are  connected  using
this  username  are  logged.

```
user=john
```

### Delimiter

The optional `delimiter` parameter defines a delimiter that is used to
distinguish columns in the log. The default delimiter is **`:::`**.

```
delimiter=:::
```

### Query_delimiter

The optional `query_delimiter` defines a delimiter that is used to
distinguish different SQL statements in a transaction.
The default query delimiter is **`@@@`**.

```
query_delimiter=@@@
```

### Named_pipe

**`named_pipe`** is the path to a named pipe, which TPM filter uses to
communicate with 3rd-party applications (e.g., [DBSeer](http://dbseer.org)).
Logging is enabled when the router receives the character '1' and logging is
disabled when the router receives the character '0' from this named pipe.
The default named pipe is **`/tmp/tpmfilter`** and logging is **disabled** by default.

	named_pipe=/tmp/tpmfilter

For example, the following command enables the logging:

	$ echo '1' > /tmp/tpmfilter

Similarly, the following command disables the logging:

	$ echo '0' > /tmp/tpmfilter


## Examples

### Example 1 - Log Transactions for Performance Analysis

You want to log every transaction with its SQL statements and latency
for future transaction performance analysis.

Add a filter with the following definition:

```
[PerformanceLogger]
type=filter
module=tpmfilter
delimiter=:::
query_delimiter=@@@
filename=/var/logs/tpm/perf.log
named_pipe=/tmp/tpmfilter

[Product Service]
type=service
router=readconnrouter
servers=server1
user=myuser
passwd=mypasswd
filters=PerformanceLogger
```

After the filter reads the character '1' from its named pipe, the following
is an example log that is generated from the above TPM filter with the above configuration:


```
1453751768:::server1:::root:::3:::UPDATE WAREHOUSE SET W_YTD = W_YTD + 900.86  WHERE W_ID = 2 @@@SELECT W_STREET_1, W_STREET_2, W_CITY, W_STATE, W_ZIP, W_NAME FROM WAREHOUSE WHERE W_ID = 2@@@UPDATE DISTRICT SET D_YTD = D_YTD + 900.86 WHERE D_W_ID = 2 AND D_ID = 5@@@SELECT D_STREET_1, D_STREET_2, D_CITY, D_STATE, D_ZIP, D_NAME FROM DISTRICT WHERE D_W_ID = 2 AND D_ID = 5@@@SELECT C_FIRST, C_MIDDLE, C_ID, C_STREET_1, C_STREET_2, C_CITY, C_STATE, C_ZIP, C_PHONE, C_CREDIT, C_CREDIT_LIM, C_DISCOUNT, C_BALANCE, C_YTD_PAYMENT, C_PAYMENT_CNT, C_SINCE FROM CUSTOMER WHERE C_W_ID = 2 AND C_D_ID = 5 AND C_LAST = 'CALLYCALLYATION' ORDER BY C_FIRST@@@UPDATE CUSTOMER SET C_BALANCE = -90026.89, C_YTD_PAYMENT = 93507.06, C_PAYMENT_CNT = 38 WHERE C_W_ID = 2 AND C_D_ID = 5 AND C_ID = 779@@@INSERT INTO HISTORY (H_C_D_ID, H_C_W_ID, H_C_ID, H_D_ID, H_W_ID, H_DATE, H_AMOUNT, H_DATA)  VALUES (5,2,779,5,2,'2016-01-25 14:56:08',900.86,'gqfla    adopdon')
1453751768:::server1:::root:::5:::UPDATE WAREHOUSE SET W_YTD = W_YTD + 3679.75  WHERE W_ID = 2 @@@SELECT W_STREET_1, W_STREET_2, W_CITY, W_STATE, W_ZIP, W_NAME FROM WAREHOUSE WHERE W_ID = 2@@@UPDATE DISTRICT SET D_YTD = D_YTD + 3679.75 WHERE D_W_ID = 2 AND D_ID = 1@@@SELECT D_STREET_1, D_STREET_2, D_CITY, D_STATE, D_ZIP, D_NAME FROM DISTRICT WHERE D_W_ID = 2 AND D_ID = 1@@@SELECT C_FIRST, C_MIDDLE, C_LAST, C_STREET_1, C_STREET_2, C_CITY, C_STATE, C_ZIP, C_PHONE, C_CREDIT, C_CREDIT_LIM, C_DISCOUNT, C_BALANCE, C_YTD_PAYMENT, C_PAYMENT_CNT, C_SINCE FROM CUSTOMER WHERE C_W_ID = 2 AND C_D_ID = 1 AND C_ID = 203@@@UPDATE CUSTOMER SET C_BALANCE = 1600482.5, C_YTD_PAYMENT = 1192789.8, C_PAYMENT_CNT = 485 WHERE C_W_ID = 2 AND C_D_ID = 1 AND C_ID = 203@@@INSERT INTO HISTORY (H_C_D_ID, H_C_W_ID, H_C_ID, H_D_ID, H_W_ID, H_DATE, H_AMOUNT, H_DATA)  VALUES (1,2,203,1,2,'2016-01-25 14:56:08',3679.75,'gqfla    uquslfu')
...
```

Note that 3 and 5 are latencies of each transaction in milliseconds.
