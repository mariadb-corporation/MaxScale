/**
 * @file mxs682_cyrillic.cpp put cyrillic letters to the table
 * - put string with Cyrillic into table
 * - check SELECT from backend
 */

#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include <iconv.h>

using namespace std;

void check_val(MYSQL* conn, TestConnections* Test)
{
    char val[256];
    Test->set_timeout(20);
    find_field(conn, "SELECT * FROM t2", "x", val);

    Test->tprintf("result: %s\n", val);

    if (strcmp("Кот", val) != 0)
    {
        Test->add_result(1, "Wrong SELECT result: %s\n", val);
    }
}

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(30);

    Mariadb_nodes* nodes;
    if (strstr(Test->test_name, "galera") != NULL)
    {
        nodes = Test->galera;
        Test->tprintf("Galera!\n");
    }
    else
    {
        nodes = Test->repl;
    }

    Test->maxscales->connect_maxscale(0);
    Test->set_timeout(30);
    nodes->connect();

    Test->set_timeout(30);
    MYSQL* conn = Test->maxscales->conn_rwsplit[0];

    execute_query_silent(conn, (char*) "DROP TABLE t2;");
    Test->try_query(conn, (char*) "CREATE TABLE t2 (x varchar(10));");
    char sql[256];
    sprintf(sql, "INSERT INTO t2 VALUES (\"Кот\");");
    Test->try_query(conn, "%s", sql);
    Test->stop_timeout();
    sleep(5);

    Test->set_timeout(30);
    check_val(Test->maxscales->conn_rwsplit[0], Test);
    check_val(Test->maxscales->conn_master[0], Test);
    check_val(Test->maxscales->conn_slave[0], Test);

    for (int i = 0; i < Test->repl->N; i++)
    {
        Test->tprintf("Node %d\n", i);
        check_val(nodes->nodes[i], Test);
    }
    Test->stop_timeout();

    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
