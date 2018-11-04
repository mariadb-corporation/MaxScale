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
password=mypasswd
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

## Log Output Format

For each transaction, the TPM filter prints its log in the following format:

\<timestamp\> | \<server\_name\> | \<user\_name\> | \<latency of the transaction\> | \<latencies of individual statements in the transaction\> (delimited by 'query_delimiter') | \<actual SQL statements\>

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
password=mypasswd
filters=PerformanceLogger
```

After the filter reads the character '1' from its named pipe, the following
is an example log that is generated from the above TPM filter with the above configuration:


```
1484086477::::server1::::root::::3::::0.165@@@@0.108@@@@0.102@@@@0.092@@@@0.121@@@@0.122@@@@0.110@@@@2.081::::UPDATE WAREHOUSE SET W_YTD = W_YTD + 3630.48  WHERE W_ID = 2 @@@@SELECT W_STREET_1, W_STREET_2, W_CITY, W_STATE, W_ZIP, W_NAME FROM WAREHOUSE WHERE W_ID = 2@@@@UPDATE DISTRICT SET D_YTD = D_YTD + 3630.48 WHERE D_W_ID = 2 AND D_ID = 9@@@@SELECT D_STREET_1, D_STREET_2, D_CITY, D_STATE, D_ZIP, D_NAME FROM DISTRICT WHERE D_W_ID = 2 AND D_ID = 9@@@@SELECT C_FIRST, C_MIDDLE, C_LAST, C_STREET_1, C_STREET_2, C_CITY, C_STATE, C_ZIP, C_PHONE, C_CREDIT, C_CREDIT_LIM, C_DISCOUNT, C_BALANCE, C_YTD_PAYMENT, C_PAYMENT_CNT, C_SINCE FROM CUSTOMER WHERE C_W_ID = 2 AND C_D_ID = 9 AND C_ID = 1025@@@@UPDATE CUSTOMER SET C_BALANCE = 1007749.25, C_YTD_PAYMENT = 465215.47, C_PAYMENT_CNT = 203 WHERE C_W_ID = 2 AND C_D_ID = 9 AND C_ID = 1025@@@@INSERT INTO HISTORY (H_C_D_ID, H_C_W_ID, H_C_ID, H_D_ID, H_W_ID, H_DATE, H_AMOUNT, H_DATA)  VALUES (9,2,1025,9,2,'2017-01-10 17:14:37',3630.48,'locfljbe    xtnfqn')
1484086477::::server1::::root::::6::::0.123@@@@0.087@@@@0.091@@@@0.098@@@@0.078@@@@0.106@@@@0.094@@@@0.074@@@@0.089@@@@0.073@@@@0.098@@@@0.073@@@@0.088@@@@0.072@@@@0.087@@@@0.071@@@@0.085@@@@0.078@@@@0.088@@@@0.098@@@@0.081@@@@0.076@@@@0.082@@@@0.073@@@@0.077@@@@0.070@@@@0.105@@@@0.093@@@@0.088@@@@0.089@@@@0.087@@@@0.087@@@@0.086@@@@1.883::::SELECT C_DISCOUNT, C_LAST, C_CREDIT, W_TAX  FROM CUSTOMER, WAREHOUSE WHERE W_ID = 2 AND C_W_ID = 2 AND C_D_ID = 10 AND C_ID = 1267@@@@SELECT D_NEXT_O_ID, D_TAX FROM DISTRICT WHERE D_W_ID = 2 AND D_ID = 10 FOR UPDATE@@@@UPDATE DISTRICT SET D_NEXT_O_ID = D_NEXT_O_ID + 1 WHERE D_W_ID = 2 AND D_ID = 10@@@@INSERT INTO OORDER (O_ID, O_D_ID, O_W_ID, O_C_ID, O_ENTRY_D, O_OL_CNT, O_ALL_LOCAL) VALUES (286871, 10, 2, 1267, '2017-01-10 17:14:37', 7, 1)@@@@INSERT INTO NEW_ORDER (NO_O_ID, NO_D_ID, NO_W_ID) VALUES ( 286871, 10, 2)@@@@SELECT I_PRICE, I_NAME , I_DATA FROM ITEM WHERE I_ID = 24167@@@@SELECT S_QUANTITY, S_DATA, S_DIST_01, S_DIST_02, S_DIST_03, S_DIST_04, S_DIST_05,        S_DIST_06, S_DIST_07, S_DIST_08, S_DIST_09, S_DIST_10 FROM STOCK WHERE S_I_ID = 24167 AND S_W_ID = 2 FOR UPDATE@@@@SELECT I_PRICE, I_NAME , I_DATA FROM ITEM WHERE I_ID = 96982@@@@SELECT S_QUANTITY, S_DATA, S_DIST_01, S_DIST_02, S_DIST_03, S_DIST_04, S_DIST_05,        S_DIST_06, S_DIST_07, S_DIST_08, S_DIST_09, S_DIST_10 FROM STOCK WHERE S_I_ID = 96982 AND S_W_ID = 2 FOR UPDATE@@@@SELECT I_PRICE, I_NAME , I_DATA FROM ITEM WHERE I_ID = 40679@@@@SELECT S_QUANTITY, S_DATA, S_DIST_01, S_DIST_02, S_DIST_03, S_DIST_04, S_DIST_05,        S_DIST_06, S_DIST_07, S_DIST_08, S_DIST_09, S_DIST_10 FROM STOCK WHERE S_I_ID = 40679 AND S_W_ID = 2 FOR UPDATE@@@@SELECT I_PRICE, I_NAME , I_DATA FROM ITEM WHERE I_ID = 31459@@@@SELECT S_QUANTITY, S_DATA, S_DIST_01, S_DIST_02, S_DIST_03, S_DIST_04, S_DIST_05,        S_DIST_06, S_DIST_07, S_DIST_08, S_DIST_09, S_DIST_10 FROM STOCK WHERE S_I_ID = 31459 AND S_W_ID = 2 FOR UPDATE@@@@SELECT I_PRICE, I_NAME , I_DATA FROM ITEM WHERE I_ID = 6143@@@@SELECT S_QUANTITY, S_DATA, S_DIST_01, S_DIST_02, S_DIST_03, S_DIST_04, S_DIST_05,        S_DIST_06, S_DIST_07, S_DIST_08, S_DIST_09, S_DIST_10 FROM STOCK WHERE S_I_ID = 6143 AND S_W_ID = 2 FOR UPDATE@@@@SELECT I_PRICE, I_NAME , I_DATA FROM ITEM WHERE I_ID = 12001@@@@SELECT S_QUANTITY, S_DATA, S_DIST_01, S_DIST_02, S_DIST_03, S_DIST_04, S_DIST_05,        S_DIST_06, S_DIST_07, S_DIST_08, S_DIST_09, S_DIST_10 FROM STOCK WHERE S_I_ID = 12001 AND S_W_ID = 2 FOR UPDATE@@@@SELECT I_PRICE, I_NAME , I_DATA FROM ITEM WHERE I_ID = 40407@@@@SELECT S_QUANTITY, S_DATA, S_DIST_01, S_DIST_02, S_DIST_03, S_DIST_04, S_DIST_05,        S_DIST_06, S_DIST_07, S_DIST_08, S_DIST_09, S_DIST_10 FROM STOCK WHERE S_I_ID = 40407 AND S_W_ID = 2 FOR UPDATE@@@@INSERT INTO ORDER_LINE (OL_O_ID, OL_D_ID, OL_W_ID, OL_NUMBER, OL_I_ID, OL_SUPPLY_W_ID,  OL_QUANTITY, OL_AMOUNT, OL_DIST_INFO) VALUES (286871,10,2,1,24167,2,7,348.31998,'btdyjesowlpzjwnmxdcsion')@@@@INSERT INTO ORDER_LINE (OL_O_ID, OL_D_ID, OL_W_ID, OL_NUMBER, OL_I_ID, OL_SUPPLY_W_ID,  OL_QUANTITY, OL_AMOUNT, OL_DIST_INFO) VALUES (286871,10,2,2,96982,2,1,4.46,'kudpnktydxbrbxibbsyvdiw')@@@@INSERT INTO ORDER_LINE (OL_O_ID, OL_D_ID, OL_W_ID, OL_NUMBER, OL_I_ID, OL_SUPPLY_W_ID,  OL_QUANTITY, OL_AMOUNT, OL_DIST_INFO) VALUES (286871,10,2,3,40679,2,7,528.43,'nhcixumgmosxlwgabvsrcnu')@@@@INSERT INTO ORDER_LINE (OL_O_ID, OL_D_ID, OL_W_ID, OL_NUMBER, OL_I_ID, OL_SUPPLY_W_ID,  OL_QUANTITY, OL_AMOUNT, OL_DIST_INFO) VALUES (286871,10,2,4,31459,2,9,341.82,'qbglbdleljyfzdpfbyziiea')@@@@INSERT INTO ORDER_LINE (OL_O_ID, OL_D_ID, OL_W_ID, OL_NUMBER, OL_I_ID, OL_SUPPLY_W_ID,  OL_QUANTITY, OL_AMOUNT, OL_DIST_INFO) VALUES (286871,10,2,5,6143,2,3,152.67,'tmtnuupaviimdmnvmetmcrc')@@@@INSERT INTO ORDER_LINE (OL_O_ID, OL_D_ID, OL_W_ID, OL_NUMBER, OL_I_ID, OL_SUPPLY_W_ID,  OL_QUANTITY, OL_AMOUNT, OL_DIST_INFO) VALUES (286871,10,2,6,12001,2,5,304.3,'ufytqwvkqxtmalhenrssfon')@@@@INSERT INTO ORDER_LINE (OL_O_ID, OL_D_ID, OL_W_ID, OL_NUMBER, OL_I_ID, OL_SUPPLY_W_ID,  OL_QUANTITY, OL_AMOUNT, OL_DIST_INFO) VALUES (286871,10,2,7,40407,2,1,30.32,'hvclpfnblxchbyluumetcqn')@@@@UPDATE STOCK SET S_QUANTITY = 65 , S_YTD = S_YTD + 7, S_ORDER_CNT = S_ORDER_CNT + 1, S_REMOTE_CNT = S_REMOTE_CNT + 0  WHERE S_I_ID = 24167 AND S_W_ID = 2@@@@UPDATE STOCK SET S_QUANTITY = 97 , S_YTD = S_YTD + 1, S_ORDER_CNT = S_ORDER_CNT + 1, S_REMOTE_CNT = S_REMOTE_CNT + 0  WHERE S_I_ID = 96982 AND S_W_ID = 2@@@@UPDATE STOCK SET S_QUANTITY = 58 , S_YTD = S_YTD + 7, S_ORDER_CNT = S_ORDER_CNT + 1, S_REMOTE_CNT = S_REMOTE_CNT + 0  WHERE S_I_ID = 40679 AND S_W_ID = 2@@@@UPDATE STOCK SET S_QUANTITY = 28 , S_YTD = S_YTD + 9, S_ORDER_CNT = S_ORDER_CNT + 1, S_REMOTE_CNT = S_REMOTE_CNT + 0  WHERE S_I_ID = 31459 AND S_W_ID = 2@@@@UPDATE STOCK SET S_QUANTITY = 86 , S_YTD = S_YTD + 3, S_ORDER_CNT = S_ORDER_CNT + 1, S_REMOTE_CNT = S_REMOTE_CNT + 0  WHERE S_I_ID = 6143 AND S_W_ID = 2@@@@UPDATE STOCK SET S_QUANTITY = 13 , S_YTD = S_YTD + 5, S_ORDER_CNT = S_ORDER_CNT + 1, S_REMOTE_CNT = S_REMOTE_CNT + 0  WHERE S_I_ID = 12001 AND S_W_ID = 2@@@@UPDATE STOCK SET S_QUANTITY = 44 , S_YTD = S_YTD + 1, S_ORDER_CNT = S_ORDER_CNT + 1, S_REMOTE_CNT = S_REMOTE_CNT + 0  WHERE S_I_ID = 40407 AND S_W_ID = 2
...
```


Note that 3 and 6 are latencies of each transaction in milliseconds, while 0.165 and 0.123 are latencies of the first statement of each transaction in milliseconds.
