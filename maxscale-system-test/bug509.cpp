/**
 * @file bug509.cpp regression case for bug 509 "rw-split router does not send last_insert_id() to master"
 *
 * - "CREATE TABLE t2 (id INT(10) NOT NULL AUTO_INCREMENT, x int,  PRIMARY KEY (id));",
 * - do a number of INSERTs first using RWsplit, then directly Galera nodes.
 * - do "select @@wsrep_node_address, last_insert_id();" and "select last_insert_id(), @@wsrep_node_address;" and compares results.
 * - do "insert into t2 (x) values (i);" 50 times and compares results of
 * "select @@wsrep_node_address, last_insert_id();" and "select last_insert_id(), @@wsrep_node_address;"
 *
 */


#include <iostream>
#include "testconnections.h"

const char * sel1 = "select @@wsrep_node_address, last_insert_id();";
const char * sel2 = "select last_insert_id(), @@wsrep_node_address;";

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(60);

    Test->galera->connect();
    Test->connect_maxscale();

    Test->tprintf("Creating table");
    Test->try_query(Test->conn_rwsplit, "DROP TABLE IF EXISTS t2;");
    Test->try_query(Test->conn_rwsplit, "CREATE TABLE t2 (id INT(10) NOT NULL AUTO_INCREMENT, x int,  PRIMARY KEY (id));");
    Test->tprintf("Doing INSERT through readwritesplit");
    Test->try_query(Test->conn_rwsplit, "START TRANSACTION");
    Test->try_query(Test->conn_rwsplit, "insert into t2 (x) values (1);");

    char last_insert_id1[1024] = "";
    char last_insert_id2[1024] = "";
    find_field(Test->conn_rwsplit, sel1, "last_insert_id()", last_insert_id1);
    find_field(Test->conn_rwsplit, sel2, "last_insert_id()", last_insert_id2);
    Test->try_query(Test->conn_rwsplit, "COMMIT");

    Test->tprintf("'%s' gave last_insert_id() %s", sel1, last_insert_id1);
    Test->tprintf("'%s' gave last_insert_id() %s", sel2, last_insert_id2);
    Test->add_result(strcmp(last_insert_id1, last_insert_id2), "last_insert_id() are different depending in which order terms are in SELECT");

    char id_str[1024];
    char str1[1024];
    int iterations = 150;

    for (int i = 100; i < iterations; i++)
    {
        Test->set_timeout(50);
        Test->try_query(Test->conn_rwsplit, "START TRANSACTION");
        Test->add_result(execute_query(Test->conn_rwsplit, "insert into t2 (x) values (%d);", i), "Query failed");

        sprintf(str1, "select * from t2 where x=%d;", i);

        find_field(Test->conn_rwsplit, sel1, "last_insert_id()", last_insert_id1);
        find_field(Test->conn_rwsplit, str1, "id", id_str);

        int n = 0;

        while (strcmp(last_insert_id1, id_str) != 0 && n < 5)
        {
            Test->tprintf("Replication is lagging");
            sleep(1);
            find_field(Test->conn_rwsplit, str1, "id", &id_str[0]);
            n++;
        }

        Test->try_query(Test->conn_rwsplit, "COMMIT");

        Test->add_result(strcmp(last_insert_id1, id_str),
                         "last_insert_id is not equal to id even after waiting 5 seconds");

        if (i % 10 == 0)
        {
            Test->tprintf("last_insert_id is %s, id is %s", last_insert_id1, id_str);
        }
    }

    Test->try_query(Test->conn_rwsplit, "DROP TABLE t2;");
    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
