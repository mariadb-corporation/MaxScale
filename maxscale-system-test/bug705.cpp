/**
 * @file bug705.cpp regression case for bug 705 ("Authentication fails when the user connects to a database when the SQL mode includes ANSI_QUOTES")
 *
 * - use only one backend
 * - derectly to backend SET GLOBAL sql_mode="ANSI"
 * - restart MaxScale
 * - check log for "Error : Loading database names for service RW_Split encountered error: Unknown column"
 */

/*
ivan.stoykov@skysql.com 2015-01-26 14:01:11 UTC
When the sql_mode includes ANSI_QUOTES, maxscale fails to execute  the SQL at  LOAD_MYSQL_DATABASE_NAMES string

https://github.com/mariadb-corporation/MaxScale/blob/master/server/core/dbusers.c
line 90:
#define LOAD_MYSQL_DATABASE_NAMES "SELECT * FROM ( (SELECT COUNT(1) AS ndbs FROM INFORMATION_SCHEMA.SCHEMATA) AS tbl1, (SELECT GRANTEE,PRIVILEGE_TYPE from INFORMATION_SCHEMA.USER_PRIVILEGES WHERE privilege_type='SHOW DATABASES' AND REPLACE(GRANTEE, \"\'\",\"\")=CURRENT_USER()) AS tbl2)"

the error log outputs that string:
"Error : Loading database names for service galera_bs_router encountered error: Unknown column ''' in 'where clause'"

I think the quotes in LOAD_MYSQL_DATABASE_NAMES and all the SQL used by MaxScale should be adjusted according to the recent sql_mode at the backend server.

How to repeat:
mysql root@centos-7-minimal:[Mon Jan 26 15:00:48 2015][(none)]> SET SESSION sql_mode = "ORACLE"; select @@sql_mode;
Query OK, 0 rows affected (0.00 sec)

+----------------------------------------------------------------------------------------------------------------------+
| @@sql_mode                                                                                                           |
+----------------------------------------------------------------------------------------------------------------------+
| PIPES_AS_CONCAT,ANSI_QUOTES,IGNORE_SPACE,ORACLE,NO_KEY_OPTIONS,NO_TABLE_OPTIONS,NO_FIELD_OPTIONS,NO_AUTO_CREATE_USER |
+----------------------------------------------------------------------------------------------------------------------+
1 row in set (0.00 sec)

mysql root@centos-7-minimal:[Mon Jan 26 15:00:55 2015][(none)]> SELECT @@innodb_version,@@version,@@version_comment, GRANTEE,PRIVILEGE_TYPE from INFORMATION_SCHEMA.USER_PRIVILEGES WHERE privilege_type='SHOW DATABASES' AND REPLACE(GRANTEE, "\'","")=CURRENT_USER();
ERROR 1054 (42S22): Unknown column '\'' in 'where clause'
mysql root@centos-7-minimal:[Mon Jan 26 15:00:57 2015][(none)]>
Comment 1 ivan.stoykov@skysql.com 2015-01-26 14:02:42 UTC
Work around: set the sql_mode without ANSI_QUOTES:

mysql root@centos-7-minimal:[Mon Jan 26 15:00:57 2015][(none)]> SET SESSION sql_mode = "MYSQL323"; select @@sql_mode;
Query OK, 0 rows affected (0.00 sec)

+------------------------------+
| @@sql_mode                   |
+------------------------------+
| MYSQL323,HIGH_NOT_PRECEDENCE |
+------------------------------+
1 row in set (0.00 sec)

mysql root@centos-7-minimal:[Mon Jan 26 15:01:52 2015][(none)]> SELECT @@innodb_version,@@version,@@version_comment, GRANTEE,PRIVILEGE_TYPE from INFORMATION_SCHEMA.USER_PRIVILEGES WHERE privilege_type='SHOW DATABASES' AND REPLACE(GRANTEE, "\'","")=CURRENT_USER();
+------------------+-----------------------+-----------------------------------+--------------------+----------------+
| @@innodb_version | @@version             | @@version_comment                 | GRANTEE            | PRIVILEGE_TYPE |
+------------------+-----------------------+-----------------------------------+--------------------+----------------+
| 5.6.21-70.0      | 10.0.15-MariaDB-wsrep | MariaDB Server, wsrep_25.10.r4144 | 'root'@'localhost' | SHOW DATABASES |
+------------------+-----------------------+-----------------------------------+--------------------+----------------+
1 row in set (0.00 sec)
Comment 2 Massimiliano 2015-01-26 14:19:45 UTC
More informations needed for "the recent sql_mode at the backend server"

Is that an issue with a particular mysql/mariadb backend version?
Comment 3 ivan.stoykov@skysql.com 2015-01-26 14:30:08 UTC
No, it is not related to any particular version.

I tested on Percona, MySQL , MariaDB 5.5, MariaDB 10.0.15 with the same result:

[Mon Jan 26 16:24:34 2015][mysql]> SET SESSION sql_mode = ""; SELECT @@sql_mode;
Query OK, 0 rows affected (0.00 sec)

+------------+
| @@sql_mode |
+------------+
|            |
+------------+
1 row in set (0.00 sec)
[Mon Jan 26 16:24:53 2015][mysql]> SELECT @@innodb_version,@@version,@@version_comment, GRANTEE,PRIVILEGE_TYPE from INFORMATION_SCHEMA.USER_PRIVILEGES WHERE privilege_type='SHOW DATABASES' AND REPLACE(GRANTEE, "\'","")=CURRENT_USER();
+------------------+-----------------+--------------------------------------------------+--------------------+----------------+
| @@innodb_version | @@version       | @@version_comment                                | GRANTEE            | PRIVILEGE_TYPE |
+------------------+-----------------+--------------------------------------------------+--------------------+----------------+
| 5.5.41-37.0      | 5.5.41-37.0-log | Percona Server (GPL), Release 37.0, Revision 727 | 'seik'@'localhost' | SHOW DATABASES |
+------------------+-----------------+--------------------------------------------------+--------------------+----------------+
1 row in set (0.00 sec)

[Mon Jan 26 16:24:57 2015][mysql]> SET SESSION sql_mode = "DB2";SELECT @@sql_mode;
Query OK, 0 rows affected (0.00 sec)

+-----------------------------------------------------------------------------------------------+
| @@sql_mode                                                                                    |
+-----------------------------------------------------------------------------------------------+
| PIPES_AS_CONCAT,ANSI_QUOTES,IGNORE_SPACE,DB2,NO_KEY_OPTIONS,NO_TABLE_OPTIONS,NO_FIELD_OPTIONS |
+-----------------------------------------------------------------------------------------------+
1 row in set (0.00 sec)

:[Mon Jan 26 16:26:19 2015][mysql]> SELECT @@innodb_version,@@version,@@version_comment, GRANTEE,PRIVILEGE_TYPE from INFORMATION_SCHEMA.USER_PRIVILEGES WHERE privilege_type='SHOW DATABASES' AND REPLACE(GRANTEE, "\'","")=CURRENT_USER();
ERROR 1054 (42S22): Unknown column '\'' in 'where clause'

mysql root@centos-7-minimal:[Mon Jan 26 14:27:33 2015][(none)]> SET SESSION sql_mode = "POSTGRESQL"; select @@sql_mode;                                                                                                                       Query OK, 0 rows affected (0.00 sec)

+------------------------------------------------------------------------------------------------------+
| @@sql_mode                                                                                           |
+------------------------------------------------------------------------------------------------------+
| PIPES_AS_CONCAT,ANSI_QUOTES,IGNORE_SPACE,POSTGRESQL,NO_KEY_OPTIONS,NO_TABLE_OPTIONS,NO_FIELD_OPTIONS |
+------------------------------------------------------------------------------------------------------+
1 row in set (0.01 sec)

mysql root@centos-7-minimal:[Mon Jan 26 14:42:23 2015][(none)]> SELECT @@innodb_version,@@version,@@version_comment, GRANTEE,PRIVILEGE_TYPE from INFORMATION_SCHEMA.USER_PRIVILEGES WHERE privilege_type='SHOW DATABASES' AND REPLACE(GRANTEE, "\'","")=CURRENT_USER();
ERROR 1054 (42S22): Unknown column '\'' in 'where clause'
mysql root@centos-7-minimal:[Mon Jan 26 14:58:57 2015][(none)]>  SET SESSION sql_mode = ""; select @@sql_mode;
Query OK, 0 rows affected (0.00 sec)

+------------+
| @@sql_mode |
+------------+
|            |
+------------+
1 row in set (0.00 sec)

mysql root@centos-7-minimal:[Mon Jan 26 14:59:03 2015][(none)]> SELECT @@innodb_version,@@version,@@version_comment, GRANTEE,PRIVILEGE_TYPE from INFORMATION_SCHEMA.USER_PRIVILEGES WHERE privilege_type='SHOW DATABASES' AND REPLACE(GRANTEE, "\'","")=CURRENT_USER();
+------------------+-----------+------------------------------+--------------------+----------------+
| @@innodb_version | @@version | @@version_comment            | GRANTEE            | PRIVILEGE_TYPE |
+------------------+-----------+------------------------------+--------------------+----------------+
| 5.6.22           | 5.6.22    | MySQL Community Server (GPL) | 'root'@'localhost' | SHOW DATABASES |
+------------------+-----------+------------------------------+--------------------+----------------+
1 row in set (0.00 sec)

mysql root@istoykov.skysql.com:[Mon Jan 26 15:28:12 2015][(none)]> SET SESSION sql_mode = "DB2"; SELECT @@sql_mode;
Query OK, 0 rows affected (0.00 sec)

+-----------------------------------------------------------------------------------------------+
| @@sql_mode                                                                                    |
+-----------------------------------------------------------------------------------------------+
| PIPES_AS_CONCAT,ANSI_QUOTES,IGNORE_SPACE,DB2,NO_KEY_OPTIONS,NO_TABLE_OPTIONS,NO_FIELD_OPTIONS |
+-----------------------------------------------------------------------------------------------+
1 row in set (0.00 sec)

mysql root@istoykov.skysql.com:[Mon Jan 26 15:28:19 2015][(none)]> SELECT @@innodb_version,@@version,@@version_comment, GRANTEE,PRIVILEGE_TYPE from INFORMATION_SCHEMA.USER_PRIVILEGES WHERE privilege_type='SHOW DATABASES' AND REPLACE(GRANTEE, "\'","")=CURRENT_USER();
ERROR 1054 (42S22): Unknown column '\'' in 'where clause'
mysql root@istoykov.skysql.com:[Mon Jan 26 15:28:32 2015][(none)]> SET SESSION sql_mode = "MYSQL40";  SELECT @@sql_mode;
Query OK, 0 rows affected (0.00 sec)

+-----------------------------+
| @@sql_mode                  |
+-----------------------------+
| MYSQL40,HIGH_NOT_PRECEDENCE |
+-----------------------------+
1 row in set (0.00 sec)

mysql root@istoykov.skysql.com:[Mon Jan 26 15:29:09 2015][(none)]> SELECT @@innodb_version,@@version,@@version_comment, GRANTEE,PRIVILEGE_TYPE from INFORMATION_SCHEMA.USER_PRIVILEGES WHERE privilege_type='SHOW DATABASES' AND REPLACE(GRANTEE, "\'","")=CURRENT_USER();
+---------------------+--------------------+-------------------+--------------------+----------------+
| @@innodb_version    | @@version          | @@version_comment | GRANTEE            | PRIVILEGE_TYPE |
+---------------------+--------------------+-------------------+--------------------+----------------+
| 5.5.38-MariaDB-35.2 | 5.5.39-MariaDB-log | MariaDB Server    | 'root'@'localhost' | SHOW DATABASES |
+---------------------+--------------------+-------------------+--------------------+----------------+
1 row in set (0.00 sec)
Comment 4 Massimiliano 2015-01-26 14:48:04 UTC
It's still not clear if the issue is related to MaxScale or it's spotted only when yoy send the statements via mysql client
Comment 5 ivan.stoykov@skysql.com 2015-01-26 16:37:32 UTC
There is at least one case that after setting the sql_mode to string :
"REAL_AS_FLOAT,PIPES_AS_CONCAT,ANSI_QUOTES,IGNORE_SPACE,ONLY_FULL_GROUP_BY,ANSI,STRICT_TRANS_TABLES,STRICT_ALL_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,TRADITIONAL,NO_AUTO_CREATE_USER,NO_ENGINE_SUBSTITUTION" at 10.0.15-MariaDB-wsrep-log  , using maxscale in this way returned an error.

$ mysql --host max-scale-host --user=test --password=xxx --port 4449 mysqlslap
ERROR 1045 (28000): Access denied for user 'test'@'IP (using password: YES) to database 'mysqlslap'

error at the maxscale log:
Error : Loading database names for service galera_bs_router encountered error: Unknown column ''' in 'where clause'.

the following test was OK:
$ mysql --host max-scale-host --user=test --password=xxx --port 4449

After switch sql_mode to '' as "mysql> set global sql_mode='';",
the connection of the user to a database seems to work OK:
$ mysql --host max-scale-host --user=test --password=xxx -D mysqlslap
Reading table information for completion of table and column names
You can turn off this feature to get a quicker startup with -A

Welcome to the MySQL monitor. Commands end with ; or \g.
Your MySQL connection id is 2532
Server version: 5.5.41-MariaDB MariaDB Server, wsrep_25.10.r4144

Copyright (c) 2000, 2013, Oracle and/or its affiliates. All rights reserved.

Oracle is a registered trademark of Oracle Corporation and/or its
affiliates. Other names may be trademarks of their respective
owners.

Type 'help;' or '\h' for help. Type '\c' to clear the current input statement.

mysql> Bye


If needed , I will prepare other test case ?
Comment 6 Massimiliano 2015-01-26 16:40:45 UTC
Yes, please provide us other test cases and we will try to reproduce it
Comment 7 Markus Mäkelä 2015-01-26 18:23:25 UTC
Changed the double quotation marks to single quotation marks because the MySQL client manual says that ANSI_QUOTES still accepts single quotes.

This can be verified by first setting sql_mode to ANSI:

set global sql_mode="ANSI";

after that, start MaxScale and the error log contains:

MariaDB Corporation MaxScale    /home/markus/build/log/skygw_err1.log Mon Jan 26 20:16:17 2015
-----------------------------------------------------------------------
--- Logging is enabled.
2015-01-26 20:16:17   Error : Loading database names for service RW Split Router encountered error: Unknown column ''' in 'where clause'.
2015-01-26 20:16:17   Error : Loading database names for service RW Split Hint Router encountered error: Unknown column ''' in 'where clause'.
2015-01-26 20:16:17   Error : Loading database names for service Read Connection Router encountered error: Unknown column ''' in 'where clause'.

After the change the error is gone.
Comment 8 Massimiliano 2015-01-26 21:16:03 UTC
I managed to reproduce it in my environment:

- created a setup with 1 server in a service named "RW_Split"

- issued SET GLOBAL sql_mode="ANSI" via mysql client to that server

- started MaxScale and found an error in the log:


2015-01-26 16:10:52   Error : Loading database names for service RW_Split encountered error: Unknown column ''' in 'where clause'.

*/


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    printf("Connecting to backend %s\n", Test->repl->IP[0]);
    fflush(stdout);
    Test->repl->connect();

    Test->tprintf("Sending SET GLOBAL sql_mode=\"ANSI\" to backend %s\n", Test->repl->IP[0]);
    execute_query(Test->repl->nodes[0], "SET GLOBAL sql_mode=\"ANSI\"");

    Test->repl->close_connections();

    Test->tprintf("Restarting MaxScale\n");

    Test->set_timeout(120);
    Test->restart_maxscale();

    Test->check_log_err((char *) "Loading database names", false);
    Test->check_log_err((char *) "Unknown column", false);

    int rval = Test->global_result;
    delete Test;
    return rval;
    //  }
}
