/**
 * Dbfwfilter prepared statement test
 *
 * Checks that both text protocol and binary protocol prepared statements are
 * properly handled.
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    test.ssh_maxscale(true, "mkdir -p /home/vagrant/rules/;"
                      "echo 'rule test1 deny columns c on_queries select' > /home/vagrant/rules/rules.txt;"
                      "echo 'users %%@%% match any rules test1' >> /home/vagrant/rules/rules.txt;"
                      "chmod a+r /home/vagrant/rules/rules.txt;");

    test.add_result(test.restart_maxscale(), "Restarting MaxScale failed");

    test.connect_maxscale();
    execute_query_silent(test.conn_rwsplit, "DROP TABLE test.t1");

    test.try_query(test.conn_rwsplit, "CREATE TABLE test.t1(a INT, b INT, c INT)");
    test.try_query(test.conn_rwsplit, "INSERT INTO test.t1 VALUES (1, 1, 1)");

    test.add_result(execute_query(test.conn_rwsplit, "PREPARE my_ps FROM 'SELECT a, b FROM test.t1'"),
                    "Text protocol preparation should succeed");
    test.add_result(execute_query(test.conn_rwsplit, "EXECUTE my_ps"),
                    "Text protocol execution should succeed");

    test.add_result(execute_query(test.conn_rwsplit, "PREPARE my_ps2 FROM 'SELECT c FROM test.t1'") == 0,
                    "Text protocol preparation should fail");
    test.add_result(execute_query(test.conn_rwsplit, "EXECUTE my_ps2") == 0,
                    "Text protocol execution should fail");

    MYSQL_STMT* stmt = mysql_stmt_init(test.conn_rwsplit);
    const char *query = "SELECT a, b FROM test.t1";

    test.add_result(mysql_stmt_prepare(stmt, query, strlen(query)), "Binary protocol preparation should succeed");
    test.add_result(mysql_stmt_execute(stmt), "Binary protocol execution should succeed");
    mysql_stmt_close(stmt);

    stmt = mysql_stmt_init(test.conn_rwsplit);
    query = "SELECT c FROM test.t1";

    test.add_result(!mysql_stmt_prepare(stmt, query, strlen(query)), "Binary protocol preparation should fail");
    mysql_stmt_close(stmt);

    test.repl->connect();
    test.try_query(test.repl->nodes[0], "DROP TABLE test.t1");

    return test.global_result;
}
