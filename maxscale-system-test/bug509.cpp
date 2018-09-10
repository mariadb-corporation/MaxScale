/**
 * @file bug509.cpp regression case for bug 509 and 507 ( "Referring to a nonexisting server in servers=...
 * doesn't even raise a warning"
 * and "rw-split router does not send last_insert_id() to master" )
 *
 * - "CREATE TABLE t2 (id INT(10) NOT NULL AUTO_INCREMENT, x int,  PRIMARY KEY (id));",
 * - do a number of INSERTs first using RWsplit, then directly Galera nodes.
 * - do "select @@wsrep_node_address, last_insert_id();" and "select last_insert_id(), @@wsrep_node_address;"
 * and compares results.
 * - do "insert into t2 (x) values (i);" 1000 times and compares results of
 * "select @@wsrep_node_address, last_insert_id();" and "select last_insert_id(), @@wsrep_node_address;"
 *
 * Test fails if results are different (after 5 seconds of waiting after last INSERT)
 */

/*
 *  Kolbe Kegel 2014-09-01 14:48:12 UTC
 *  For some reason, the order of terms in the field list of a SELECT statement influences how the rw-split
 * router decides where to send a statement.
 *
 *  mariadb> select @@wsrep_node_address, last_insert_id();
 +----------------------+------------------+
 | @@wsrep_node_address | last_insert_id() |
 +----------------------+------------------+
 | 192.168.30.31        |                7 |
 +----------------------+------------------+
 |  1 row in set (0.00 sec)
 |
 |  mariadb> select last_insert_id(), @@wsrep_node_address;
 +------------------+----------------------+
 | last_insert_id() | @@wsrep_node_address |
 +------------------+----------------------+
 |                0 | 192.168.30.33        |
 +------------------+----------------------+
 |  1 row in set (0.00 sec)
 |  Comment 1 Vilho Raatikka 2014-09-03 20:44:17 UTC
 |  Added code to detect last_insert_id() function and now both types of elements are routed to master and
 |their order of appearance doesn't matter.
 */


#include <iostream>
#include "testconnections.h"

const char* sel1 = "select @@wsrep_node_address, last_insert_id();";
const char* sel2 = "select last_insert_id(), @@wsrep_node_address;";

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(60);

    Test->galera->connect();
    Test->maxscales->connect_maxscale(0);

    if (Test->galera->N < 3)
    {
        Test->tprintf("There is not enoght nodes for test\n");
        delete Test;
        exit(1);
    }

    Test->tprintf("Creating table\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char*) "DROP TABLE IF EXISTS t2;");
    Test->try_query(Test->maxscales->conn_rwsplit[0],
                    (char*) "CREATE TABLE t2 (id INT(10) NOT NULL AUTO_INCREMENT, x int,  PRIMARY KEY (id));");
    Test->tprintf("Doing INSERTs\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char*) "insert into t2 (x) values (1);");

    Test->try_query(Test->galera->nodes[0], (char*) "insert into t2 (x) values (2);");
    Test->try_query(Test->galera->nodes[0], (char*) "insert into t2 (x) values (3);");

    Test->try_query(Test->galera->nodes[1], (char*) "insert into t2 (x) values (4);");
    Test->try_query(Test->galera->nodes[1], (char*) "insert into t2 (x) values (5);");
    Test->try_query(Test->galera->nodes[1], (char*) "insert into t2 (x) values (6);");

    Test->try_query(Test->galera->nodes[2], (char*) "insert into t2 (x) values (7);");
    Test->try_query(Test->galera->nodes[2], (char*) "insert into t2 (x) values (8);");
    Test->try_query(Test->galera->nodes[2], (char*) "insert into t2 (x) values (9);");
    Test->try_query(Test->galera->nodes[2], (char*) "insert into t2 (x) values (10);");

    Test->stop_timeout();
    Test->repl->sync_slaves();

    Test->tprintf("Trying \n");
    char last_insert_id1[1024];
    char last_insert_id2[1024];
    if ((
            find_field(Test->maxscales->conn_rwsplit[0],
                       sel1,
                       "last_insert_id()",
                       &last_insert_id1[0])
            != 0 ) || (
            find_field(Test->maxscales->conn_rwsplit[0],
                       sel2,
                       "last_insert_id()",
                       &last_insert_id2[0])
            != 0 ))
    {
        Test->tprintf("last_insert_id() fied not found!!\n");
        delete Test;
        exit(1);
    }
    else
    {
        Test->tprintf("'%s' gave last_insert_id() %s\n", sel1, last_insert_id1);
        Test->tprintf("'%s' gave last_insert_id() %s\n", sel2, last_insert_id2);
        Test->add_result(strcmp(last_insert_id1, last_insert_id2),
                         "last_insert_id() are different depending in which order terms are in SELECT\n");
    }

    char id_str[1024];
    char str1[1024];
    int iterations = 150;

    for (int i = 100; i < iterations; i++)
    {
        Test->set_timeout(50);
        Test->add_result(execute_query(Test->maxscales->conn_rwsplit[0],
                                       "insert into t2 (x) values (%d);",
                                       i),
                         "Query failed");

        sprintf(str1, "select * from t2 where x=%d;", i);

        find_field(Test->maxscales->conn_rwsplit[0], sel1, "last_insert_id()", &last_insert_id1[0]);
        find_field(Test->maxscales->conn_rwsplit[0], str1, "id", &id_str[0]);

        int n = 0;

        while (strcmp(last_insert_id1, id_str) != 0 && n < 5)
        {
            Test->tprintf("Replication is lagging");
            sleep(1);
            find_field(Test->maxscales->conn_rwsplit[0], str1, "id", &id_str[0]);
            n++;
        }

        Test->add_result(strcmp(last_insert_id1, id_str),
                         "last_insert_id is not equal to id even after waiting 5 seconds");

        if (i % 10 == 0)
        {
            Test->tprintf("last_insert_id is %s, id is %s", last_insert_id1, id_str);
        }
    }

    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
