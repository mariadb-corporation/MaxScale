/**
 * @file mxs682_cyrillic.cpp put cyrillic letters to the table
 * - put string with Cyrillic into table
 * - check SELECT from backend
 */

#include <maxtest/testconnections.hh>

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
    if (test.repl)
    {
        test.repl->connect();
        test.repl->sync_slaves();
        test.repl->disconnect();
    }
    else
    {
        sleep(10);
    }

    test.set_timeout(60);
    test.maxscales->connect();
    check_val(test.maxscales->conn_rwsplit[0], test);
    check_val(test.maxscales->conn_master, test);
    check_val(test.maxscales->conn_slave, test);
    test.maxscales->disconnect();

    nodes->connect();
    for (int i = 0; i < nodes->N; i++)
    {
        check_val(nodes->nodes[i], test);
    }
    nodes->disconnect();

    return test.global_result;
}
