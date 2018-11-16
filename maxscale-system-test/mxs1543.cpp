/**
 * MXS-1543: https://jira.mariadb.org/browse/MXS-1543
 *
 * Avrorouter doesn't detect MIXED or STATEMENT format replication
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections::check_nodes(false);
    TestConnections test(argc, argv);

    test.replicate_from_master();

    test.repl->connect();
    execute_query(test.repl->nodes[0], "CREATE OR REPLACE TABLE t1 (data VARCHAR(30))");
    execute_query(test.repl->nodes[0], "INSERT INTO t1 VALUES ('ROW')");
    execute_query(test.repl->nodes[0], "SET binlog_format=STATEMENT");
    execute_query(test.repl->nodes[0], "FLUSH LOGS");
    execute_query(test.repl->nodes[0], "INSERT INTO t1 VALUES ('STATEMENT')");
    execute_query(test.repl->nodes[0], "SET binlog_format=ROW");
    execute_query(test.repl->nodes[0], "FLUSH LOGS");
    execute_query(test.repl->nodes[0], "INSERT INTO t1 VALUES ('ROW2')");

    // Wait for the avrorouter to process the data
    sleep(10);
    test.log_includes(0, "Possible STATEMENT or MIXED");

    test.revert_replicate_from_master();

    return test.global_result;
}
