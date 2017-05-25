/**
 * @file bug507.cpp regression case for bug 507 ( "rw-split router does not send last_insert_id() to master" )
 *
 * - "CREATE TABLE t2 (id INT(10) NOT NULL AUTO_INCREMENT, x int,  PRIMARY KEY (id));",
 * - do INSERT using RWsplit
 * - do "select last_insert_id(), @@server_id" using RWSplit and directly to Master, compare @@server_id
 *
 */

/*
Kolbe Kegel 2014-09-01 14:45:56 UTC
After inserting a row via the rw-split router, a call to last_insert_id() can go to a slave, producing bad results.

mariadb> select * from t1;
+----+
| id |
+----+
|  1 |
|  4 |
+----+
2 rows in set (0.00 sec)

mariadb> insert into t1 values ();
Query OK, 1 row affected (0.00 sec)

mariadb> select * from t1;
+----+
| id |
+----+
|  1 |
|  4 |
|  7 |
+----+
3 rows in set (0.00 sec)

mariadb> select last_insert_id();
+------------------+
| last_insert_id() |
+------------------+
|                0 |
+------------------+
1 row in set (0.00 sec)

mariadb> select @@wsrep_node_address, last_insert_id();
+----------------------+------------------+
| @@wsrep_node_address | last_insert_id() |
+----------------------+------------------+
| 192.168.30.31        |                7 |
+----------------------+------------------+
1 row in set (0.00 sec)
Comment 1 Vilho Raatikka 2014-09-01 17:51:45 UTC
last_inserted_id() belongs to UNKNOWN_FUNC class to which many read-only system functions belong too. Thus last_inserted_id() was routed to any slave.

Unfortunately I can't confirm wrong behavior since running the same sequence generates same output when connected directly to MariaDB backend. Perhaps there is something required for the table t1 which is not included here?
Comment 2 Vilho Raatikka 2014-09-01 20:01:35 UTC
An autoincrement attribute was missing.
*/


#include <iostream>
#include "testconnections.h"

const char * sel1 = "select last_insert_id(), @@server_id";

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->repl->connect();
    Test->connect_maxscale();

    if (Test->repl->N < 3)
    {
        Test->tprintf("There is not enoght nodes for test\n");
        delete Test;
        exit(1);
    }

    Test->tprintf("Creating table\n");
    fflush(stdout);
    Test->try_query(Test->conn_rwsplit, (char *) "DROP TABLE IF EXISTS t2");
    Test->try_query(Test->conn_rwsplit,
                    (char *) "CREATE TABLE t2 (id INT(10) NOT NULL AUTO_INCREMENT, x int,  PRIMARY KEY (id));");
    Test->tprintf("Doing INSERTs\n");
    fflush(stdout);
    Test->try_query(Test->conn_rwsplit, (char *) "insert into t2 (x) values (1);");

    Test->stop_timeout();
    Test->repl->sync_slaves();

    Test->set_timeout(20);
    Test->tprintf("Trying \n");
    char last_insert_id1[1024];
    char last_insert_id2[1024];
    if ( (
                find_field(
                    Test->conn_rwsplit, sel1,
                    "@@server_id", &last_insert_id1[0])
                != 0 ) || (
                find_field(
                    Test->repl->nodes[0], sel1,
                    "@@server_id", &last_insert_id2[0])
                != 0 ))
    {
        Test->tprintf("@@server_id fied not found!!\n");
        delete Test;
        exit(1);
    }
    else
    {
        Test->tprintf("'%s' to RWSplit gave @@server_id %s\n", sel1, last_insert_id1);
        Test->tprintf("'%s' directly to master gave @@server_id %s\n", sel1, last_insert_id2);
        Test->add_result(strcmp(last_insert_id1, last_insert_id2),
                         "last_insert_id() are different depending in which order terms are in SELECT\n");
    }

    Test->close_maxscale_connections();
    Test->repl->close_connections();

    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
