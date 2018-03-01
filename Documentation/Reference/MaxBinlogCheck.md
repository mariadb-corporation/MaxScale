# Maxbinlogcheck, the MariaDB binlog check utility

# Overview

Maxbinlogcheck is a command line utility for checking binlogfiles. The files may
have been downloaded by the MariaDB MaxScale binlog router or they may be
MariaDB binlog files stored in a database server acting as a master in a
replication environment. Maxbinlogcheck checks the binlog files against any
corruption and stored incomplete transactions and reports a transaction summary
after reading all the events. It may optionally truncate the binlog file.

Maxbinlogcheck supports:

* MariaDB 5.5 and MySQL 5.6
* MariaDB 10.0 and 10.1 with a command line option

# Running maxbinlogcheck

```
# /usr/local/bin/maxbinlogcheck /path_to_file/bin.000002
```

# Command Line Switches

The maxbinlogcheck command accepts a number of switches

<table>
  <tr>
    <td>Switch</td>
    <td>Long Option</td>
    <td>Description</td>
  </tr>
  <tr>
    <td>-f</td> <td>--fix</td> <td>If set the binlog file will be truncated at
    last safe transaction pos in case of any error</td>
  </tr>
  <tr>
    <td>-M</td>
    <td>--mariadb10</td>
    <td>Checks the current binlog against MariaDB 10.0.x events</td>
  </tr>
  <tr>
    <td>-d</td>
    <td>--debug</td>
    <td>Sets the debug mode. If set the FD Events, Rotate events and
    opening/closing transactions are displayed.</td>
  </tr>
  <tr>
    <td>-?</td>
    <td>--help</td>
    <td>Prints usage information regarding maxbinlogcheck</td>
  </tr>
  <tr>
    <td>-V</td>
    <td>--version</td>
    <td>Prints the maxbinlogcheck version information</td>
  </tr>
  <tr>
    <td>-K</td>
    <td>--key_file</td>
    <td>AES Key file for MariaDB 10.1 binlog file decryption</td>
  </tr>
  <tr>
    <td>-A</td>
    <td>--aes_algo</td>
    <td>AES Algorithm for MariaDB 10.1 binlog file decryption (default=AES_CBC,
    AES_CTR)</td>
  </tr>
    <tr>
    <td>-H</td>
    <td>--header</td>
    <td>Prints the binlog event header</td>
  </tr>
</table>

## Example without debug:

1) No transactions

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/mar-bin.000002
2015-09-08 09:38:03   maxbinlogcheck 1.0.0
2015-09-08 09:38:03   Checking /servers/binlogs/new-trx/mar-bin.000002 (mar-bin.000002), size 290 bytes
2015-09-08 09:38:03   Check retcode: 0, Binlog Pos = 290
```

2) With complete transactions

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/mar-bin.000004
2015-09-08 09:38:35   maxbinlogcheck 1.0.0
2015-09-08 09:38:35   Checking /servers/binlogs/new-trx/mar-bin.000004 (mar-bin.000004), size 38738629 bytes
2015-09-08 09:38:36   Transaction Summary:
			Description                    Total          Average              Max
			No. of Transactions            21713
			No. of Events                 186599              8.6             5082
			No. of Bytes                     36M               1k               5M
2015-09-08 09:38:36   Check retcode: 0, Binlog Pos = 38738629
```

## Example with debug:

1) one complete transaction

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/mar-bin.000001 -d
2015-09-08 09:36:49   maxbinlogcheck 1.0.0
2015-09-08 09:36:49   Checking /servers/binlogs/new-trx/mar-bin.000001 (mar-bin.000001), size 590760698 bytes
2015-09-08 09:36:49   - Format Description event FDE @ 4, size 241
2015-09-08 09:36:49          FDE ServerVersion [                                5.5.35-MariaDB-log]
2015-09-08 09:36:49          FDE Header EventLength 19, N. of supported MySQL/MariaDB events 160
2015-09-08 09:36:49          FDE Checksum alg desc 1, alg type BINLOG_CHECKSUM_ALG_CRC32
2015-09-08 09:36:49   > Transaction starts @ pos 572
2015-09-08 09:36:49          Transaction XID @ pos 572, closing @ 590760644
2015-09-08 09:36:49   < Transaction @ pos 572, is now closed @ 590760644. 18001 events seen
2015-09-08 09:36:49   End of binlog file [mar-bin.000001] at 590760698.
2015-09-08 09:36:49   Transaction Summary:
			Description                    Total          Average              Max
			No. of Transactions                1
			No. of Events                  18001          18001.0            18001
			No. of Bytes                    563M             563M             563M
2015-09-08 09:36:49   Check retcode: 0, Binlog Pos = 590760698
```

2) some transactions

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/mar-bin.000004 -d
2015-09-08 10:19:35   maxbinlogcheck 1.0.0
2015-09-08 10:19:35   Checking /servers/binlogs/new-trx/mar-bin.000004 (mar-bin.000004), size 38738629 bytes
2015-09-08 10:19:35   - Format Description event FDE @ 4, size 241
2015-09-08 10:19:35          FDE ServerVersion [                                5.5.35-MariaDB-log]
2015-09-08 10:19:35          FDE Header EventLength 19, N. of supported MySQL/MariaDB events 160
2015-09-08 10:19:35          FDE Checksum alg desc 1, alg type BINLOG_CHECKSUM_ALG_CRC32
2015-09-08 10:19:35   > Transaction starts @ pos 971
2015-09-08 10:19:35          Transaction XID @ pos 971, closing @ 1128
2015-09-08 10:19:35   < Transaction @ pos 971, is now closed @ 1128. 3 events seen
...
2015-09-08 10:19:51   > Transaction starts @ pos 33440561
2015-09-08 10:19:51          Transaction XID @ pos 33440561, closing @ 33440763
2015-09-08 10:19:51   < Transaction @ pos 33440561, is now closed @ 33440763. 3 events seen
2015-09-08 10:19:51   > Transaction starts @ pos 33440794
2015-09-08 10:19:51          Transaction XID @ pos 33440794, closing @ 38738553
2015-09-08 10:19:51   < Transaction @ pos 33440794, is now closed @ 38738553. 5082 events seen
2015-09-08 10:19:51   - Rotate event @ 38738584, next file is [mar-bin.000005] @ 4
2015-09-08 10:19:51   End of binlog file [mar-bin.000004] at 38738629.
2015-09-08 10:19:51   Transaction Summary:
			Description                    Total          Average              Max
			No. of Transactions            21713
			No. of Events                 186599              8.6             5082
			No. of Bytes                     36M               1k               5M
2015-09-08 10:19:51   Check retcode: 0, Binlog Pos = 38738629
```

3) No transactions

```
2015-09-08 09:41:02   Check retcode: 0, Binlog Pos = 290
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/mar-bin.000002 -d
2015-09-08 09:41:08   maxbinlogcheck 1.0.0
2015-09-08 09:41:08   Checking /servers/binlogs/new-trx/mar-bin.000002 (mar-bin.000002), size 290 bytes
2015-09-08 09:41:08   - Format Description event FDE @ 4, size 241
2015-09-08 09:41:08          FDE ServerVersion [                                5.5.35-MariaDB-log]
2015-09-08 09:41:08          FDE Header EventLength 19, N. of supported MySQL/MariaDB events 160
2015-09-08 09:41:08          FDE Checksum alg desc 1, alg type BINLOG_CHECKSUM_ALG_CRC32
2015-09-08 09:41:08   - Rotate event @ 245, next file is [mar-bin.000003] @ 4
2015-09-08 09:41:08   End of binlog file [mar-bin.000002] at 290.
2015-09-08 09:41:08   Check retcode: 0, Binlog Pos = 290
```

## Fixing a corrupted binlog file

This file is corrupted, as reported by the utility:

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/bin.000002
2015-09-08 10:03:16   maxbinlogcheck 1.0.0
2015-09-08 10:03:16   Checking /servers/binlogs/new-trx/bin.000002 (bin.000002), size 109498 bytes
2015-09-08 10:03:16   Event size error: size 0 at 290.
2015-09-08 10:03:16   warning : an error has been found. Setting safe pos to 245, current pos 290
2015-09-08 10:03:16   Check retcode: 1, Binlog Pos = 245
```

The suggested safe pos is 245.

Use -f option for fix with debug:

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/bin.000002 -d -f
2015-09-08 09:56:52   maxbinlogcheck 1.0.0
2015-09-08 09:56:52   Checking /servers/binlogs/new-trx/bin.000002 (bin.000002), size 109498 bytes
2015-09-08 09:56:52   - Format Description event FDE @ 4, size 241
2015-09-08 09:56:52          FDE ServerVersion [                                5.5.35-MariaDB-log]
2015-09-08 09:56:52          FDE Header EventLength 19, N. of supported MySQL/MariaDB events 160
2015-09-08 09:56:52          FDE Checksum alg desc 1, alg type BINLOG_CHECKSUM_ALG_CRC32
2015-09-08 09:56:52   - Rotate event @ 245, next file is [mar-bin.000003] @ 4
2015-09-08 09:56:52   Event size error: size 0 at 290.
2015-09-08 09:56:52   warning : an error has been found. Setting safe pos to 245, current pos 290
2015-09-09 09:56:52   Binlog file bin.000002 has been truncated at 245
2015-09-08 09:56:52   Check retcode: 1, Binlog Pos = 245
```

Check it again, last pos will be 245 and no errors will be reported:

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/bin.000002 -d
2015-09-08 09:56:56   maxbinlogcheck 1.0.0
2015-09-08 09:56:56   Checking /servers/binlogs/new-trx/bin.000002 (bin.000002), size 245 bytes
2015-09-08 09:56:56   - Format Description event FDE @ 4, size 241
2015-09-08 09:56:56          FDE ServerVersion [                                5.5.35-MariaDB-log]
2015-09-08 09:56:56          FDE Header EventLength 19, N. of supported MySQL/MariaDB events 160
2015-09-08 09:56:56          FDE Checksum alg desc 1, alg type BINLOG_CHECKSUM_ALG_CRC32
2015-09-08 09:56:56   End of binlog file [bin.000002] at 245.
2015-09-08 09:56:56   Check retcode: 0, Binlog Pos = 245
```

## Detection of an incomplete big transaction

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/mar-bin.000003
2015-09-08 10:10:21   maxbinlogcheck 1.0.0
2015-09-08 10:10:21   Checking /servers/binlogs/new-trx/mar-bin.000003 (mar-bin.000003), size 16476284 bytes
2015-09-08 10:10:21   Warning : pending transaction has been found. Setting safe pos to 572, current pos 16476284
2015-09-08 10:10:21   Check retcode: 0, Binlog Pos = 572
```

with debug option:

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/mar-bin.000003 -d
2015-09-08 10:11:08   maxbinlogcheck 1.0.0
2015-09-08 10:11:08   Checking /servers/binlogs/new-trx/mar-bin.000003 (mar-bin.000003), size 16476284 bytes
2015-09-08 10:11:08   - Format Description event FDE @ 4, size 241
2015-09-08 10:11:08          FDE ServerVersion [                                5.5.35-MariaDB-log]
2015-09-08 10:11:08          FDE Header EventLength 19, N. of supported MySQL/MariaDB events 160
2015-09-08 10:11:08          FDE Checksum alg desc 1, alg type BINLOG_CHECKSUM_ALG_CRC32
2015-09-08 10:11:08   > Transaction starts @ pos 572
2015-09-08 10:11:08   Warning : pending transaction has been found. Setting safe pos to 572, current pos 16476284
2015-09-08 10:11:08   End of binlog file [mar-bin.000003] at 16476284.
2015-09-08 10:11:08   Check retcode: 0, Binlog Pos = 572
```

Retcode is 0 as the transaction may proceed over time, example:

Another check ...

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/mar-bin.000003 -d
2015-09-08 10:17:13   maxbinlogcheck 1.0.0
2015-09-08 10:17:13   Checking /servers/binlogs/new-trx/mar-bin.000003 (mar-bin.000003), size 569296364 bytes
2015-09-08 10:17:13   - Format Description event FDE @ 4, size 241
2015-09-08 10:17:13          FDE ServerVersion [                                5.5.35-MariaDB-log]
2015-09-08 10:17:13          FDE Header EventLength 19, N. of supported MySQL/MariaDB events 160
2015-09-08 10:17:13          FDE Checksum alg desc 1, alg type BINLOG_CHECKSUM_ALG_CRC32
2015-09-08 10:17:13   > Transaction starts @ pos 572
2015-09-08 10:17:14   End of binlog file [mar-bin.000003] at 577567062.
2015-09-08 10:17:14   Check retcode: 0, Binlog Pos = 572
```

And finally big transaction is now done.

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/mar-bin.000003 -d
2015-09-08 10:17:15   maxbinlogcheck 1.0.0
2015-09-08 10:17:15   Checking /servers/binlogs/new-trx/mar-bin.000003 (mar-bin.000003), size 590760698 bytes
2015-09-08 10:17:15   - Format Description event FDE @ 4, size 241
2015-09-08 10:17:15          FDE ServerVersion [                                5.5.35-MariaDB-log]
2015-09-08 10:17:15          FDE Header EventLength 19, N. of supported MySQL/MariaDB events 160
2015-09-08 10:17:15          FDE Checksum alg desc 1, alg type BINLOG_CHECKSUM_ALG_CRC32
2015-09-08 10:17:15   > Transaction starts @ pos 572
2015-09-08 10:17:16          Transaction XID @ pos 572, closing @ 590760644
2015-09-08 10:17:16   < Transaction @ pos 572, is now closed @ 590760644. 18001 events seen
2015-09-08 10:17:16   End of binlog file [mar-bin.000003] at 590760698.
2015-09-08 10:17:16   Transaction Summary:
			Description                    Total          Average              Max
			No. of Transactions                1
			No. of Events                  18001          18001.0            18001
			No. of Bytes                    563M             563M             563M
2015-09-08 10:17:16   Check retcode: 0, Binlog Pos = 590760698
```

**Note**

With current maxbinlogcheck it's not possible to fix a binlog with incomplete
transaction and no other errors

If that is really desired it will be possible with UNIX command line:

```
# truncate /servers/binlogs/new-trx/mar-bin.000003 --size=572
```

In case of an error and incomplete transaction, the fix will work

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/mar-bin.000003 -d -f
2015-09-08 10:35:57   maxbinlogcheck 1.0.0
2015-09-08 10:35:57   Checking /servers/binlogs/new-trx/mar-bin.000003 (mar-bin.000003), size 282580902 bytes
2015-09-08 10:35:57   - Format Description event FDE @ 4, size 241
2015-09-08 10:35:57          FDE ServerVersion [                                5.5.35-MariaDB-log]
2015-09-08 10:35:57          FDE Header EventLength 19, N. of supported MySQL/MariaDB events 160
2015-09-08 10:35:57          FDE Checksum alg desc 1, alg type BINLOG_CHECKSUM_ALG_CRC32
2015-09-08 10:35:57   > Transaction starts @ pos 572
2015-09-08 10:35:57   Short read when reading the event at 304898502 in mar-bin.000003. Expected 65563 bytes got 15911 bytes.
2015-09-08 10:35:57   warning : an error has been found. Setting safe pos to 572, current pos 304898502
2015-09-09 10:35:57   Binlog file bin.000003 has been truncated at 572
2015-09-08 10:35:57   Check retcode: 1, Binlog Pos = 572
```

Check result:

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck /servers/binlogs/new-trx/mar-bin.000003 -d
2015-09-08 10:54:17   maxbinlogcheck 1.0.0
2015-09-08 10:54:17   Checking /servers/binlogs/new-trx/mar-bin.000003 (mar-bin.000003), size 572 bytes
2015-09-08 10:54:17   - Format Description event FDE @ 4, size 241
2015-09-08 10:54:17          FDE ServerVersion [                                5.5.35-MariaDB-log]
2015-09-08 10:54:17          FDE Header EventLength 19, N. of supported MySQL/MariaDB events 160
2015-09-08 10:54:17          FDE Checksum alg desc 1, alg type BINLOG_CHECKSUM_ALG_CRC32
2015-09-08 10:54:17   End of binlog file [mar-bin.000003] at 572.
2015-09-08 10:54:17   Check retcode: 0, Binlog Pos = 572
```

### MariaDB 10 binlog check

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck -M -d /mariadb-10.0.11/data/mysql-bin.000008
2015-09-08 12:49:18   maxbinlogcheck 1.0.0
2015-09-08 12:49:18   Checking /mariadb-10.0.11/data/mysql-bin.000008 (mysql-bin.000008), size 1215327 bytes
2015-09-08 12:49:18   - Format Description event FDE @ 4, size 244
2015-09-08 12:49:18          FDE ServerVersion [                               10.0.11-MariaDB-log]
2015-09-08 12:49:18          FDE Header EventLength 19, N. of supported MySQL/MariaDB events 163
2015-09-08 12:49:18          FDE Checksum alg desc 0, alg type NONE or UNDEF
2015-09-08 12:49:18   > MariaDB 10 Transaction (GTID 0-29-60) starts @ pos 802
2015-09-08 12:49:18          Transaction XID @ pos 802, closing @ 1214943
2015-09-08 12:49:18   < Transaction @ pos 802, is now closed @ 1214943. 76 events seen
2015-09-08 12:49:18   End of binlog file [mysql-bin.000008] at 1215327.
2015-09-08 12:49:18   Transaction Summary:
			Description                    Total          Average              Max
			No. of Transactions                1
			No. of Events                     76             76.0               76
			No. of Bytes                    1.2M             1.2M             1.2M
2015-09-08 12:49:18   Check retcode: 0, Binlog Pos = 1215327
```
### MariaDB 10.1 encrypted binlogs

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck -M -d /mariadb-10.1.16/data/mysql-bin.000008 -K /var/binlogs/key_file.txt -A AES_CTR
2016-12-07 16:18:35   notice : maxbinlogcheck 2.1.0
2016-12-07 16:18:35   notice : Decrypting binlog file with algorithm: aes_cbc, KEY len 256 bits
2016-12-07 16:18:35   notice : Checking /mariadb-10.1.16/data/mysql-bin.000008 (mysql-bin.000008), size 418 bytes
2016-12-07 16:18:35   debug  : - Format Description event FDE @ 4, size 245, time 1481044895 (Tue Dec  6 18:21:35 2016)
2016-12-07 16:18:35   debug  :        FDE ServerVersion [                                   10.1.16-MariaDB]
2016-12-07 16:18:35   debug  :        FDE Header EventLength 19, N. of supported MySQL/MariaDB events 164
2016-12-07 16:18:35   debug  :        FDE Checksum alg desc 1, alg type BINLOG_CHECKSUM_ALG_CRC32
2016-12-07 16:18:35   debug  : - START_ENCRYPTION event @ 249, size 40, next pos is @ 289, flags 0
2016-12-07 16:18:35   debug  :         Encryption scheme: 1, key_version: 1, nonce: 6732673744475A1F5852575C
2016-12-07 16:18:35   debug  : End of binlog file [mysql-bin.000003] at 418.
2016-12-07 16:18:35   notice : 1481044895 @ 249, Start Encryption Event, (Tue Dec  6 18:21:35 2016), First EventTime
2016-12-07 16:18:35   notice : 0 @ 375, Binlog Checkpoint Event, (Thu Jan  1 01:00:00 1970), Last EventTime
2016-12-07 16:18:35   notice : Check retcode: 0, Binlog Pos = 418
```
Key File content example: /var/binlogs/key_file.txt

First two bytes are: the encryption scheme, it must be 1, and the ';' separator.
Following bytes are the HEX representation of the key (length must be 16, 24 or
32). The example shows a 32 bytes key in HEX format (64 bytes):

```
1;666f6f62617220676f657320746f207468652062617220666f7220636f66666565
```

### Binlog event header

```
[root@maxscale-02 build]# /usr/local/bin/maxbinlogcheck -M -d /mysql.5.6.17/data/mysql-bin.000001 -H
2016-12-07 16:23:02   notice : maxbinlogcheck 2.1.0
2016-12-07 16:23:02   notice : Checking /mysql.5.6.17/data/mysql-bin.000001 (mysql-bin.000001), size 173 bytes
2016-12-07 16:23:02   debug  : - Format Description event FDE @ 4, size 116, time 1455024737 (Tue Feb  9 14:32:17 2016)
2016-12-07 16:23:02   debug  :        FDE ServerVersion [                                        5.6.17-log]
2016-12-07 16:23:02   debug  :        FDE Header EventLength 19, N. of supported MySQL/MariaDB events 35
2016-12-07 16:23:02   debug  :        FDE Checksum alg desc 1, alg type BINLOG_CHECKSUM_ALG_CRC32
2016-12-07 16:23:02   debug  :         ==== Event Header ====
                                       Event time 1455024737
                                       Event Type 15 (Format Description Event)
                                       Server Id 1
                                       NextPos 120
                                       Flags 0
2016-12-07 16:23:02   debug  :         CRC32 0xdc0879e8
2016-12-07 16:23:02   debug  :         ==== Event Header ====
                                       Event time 1455025495
                                       Event Type 4 (Rotate Event)
                                       Server Id 1
                                       NextPos 173
                                       Flags 0
2016-12-07 16:23:02   debug  :         CRC32 0xfff32f78
2016-12-07 16:23:02   debug  : - Rotate event @ 120, next file is [mysql-bin.000002] @ 4
2016-12-07 16:23:02   debug  : End of binlog file [mysql-bin.000001] at 173.
2016-12-07 16:23:02   notice : 1455025495 @ 120, Rotate Event, (Tue Feb  9 14:44:55 2016), First EventTime
2016-12-07 16:23:02   notice : 1455025495 @ 120, Rotate Event, (Tue Feb  9 14:44:55 2016), Last EventTime
2016-12-07 16:23:02   notice : Check retcode: 0, Binlog Pos = 173
```
