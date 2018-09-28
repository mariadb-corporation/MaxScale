/**
 * @file mxs682_cyrillic.cpp put cyrillic letters to the table
 * - put string with Cyrillic into table
 * - check SELECT from backend
 */

#include "testconnections.h"

void check_val(MYSQL* conn, TestConnections& test)
{
    char val[256] = "<failed to read value>";
    test.set_timeout(30);
    find_field(conn, "SELECT * FROM t2", "x", val);
    test.tprintf("result: %s\n", val);
    test.add_result(strcmp("Кот", val) != 0, "Wrong SELECT result: %s\n", val);
    test.stop_timeout();
}

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);
    Mariadb_nodes* nodes = strstr(test.test_name, "galera") ? test.galera : test.repl;

    test.set_timeout(60);

    test.maxscales->connect();

    auto conn = test.maxscales->conn_rwsplit[0];
    execute_query_silent(conn, "DROP TABLE t2;");
    test.try_query(conn, "CREATE TABLE t2 (x varchar(10));");
    test.try_query(conn, "INSERT INTO t2 VALUES (\"Кот\");");

    test.maxscales->disconnect();

    test.stop_timeout();
    test.repl->connect();
    test.repl->sync_slaves();

    test.set_timeout(60);
    check_val(test.maxscales->conn_rwsplit[0], test);
    check_val(test.maxscales->conn_master[0], test);
    check_val(test.maxscales->conn_slave[0], test);

    for (int i = 0; i < test.repl->N; i++)
    {
        check_val(nodes->nodes[i], test);
    }

    return test.global_result;
}
