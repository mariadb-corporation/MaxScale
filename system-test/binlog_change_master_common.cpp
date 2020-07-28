#include "testconnections.h"
#include <functional>

void run_test(TestConnections& test, std::function<void(MYSQL*)> cb)
{
    test.set_timeout(120);
    test.start_binlog(0);
    test.repl->connect();

    // Create a table and insert some data
    execute_query(test.repl->nodes[0], "CREATE OR REPLACE TABLE test.t1 (id INT)");

    for (int i = 0; i < 25; i++)
    {
        test.try_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (%d)", i);
    }

    // Sync the candidate master
    std::string binlog_pos = get_row(test.repl->nodes[0], "SELECT @@gtid_binlog_pos")[0];
    execute_query(test.repl->nodes[2], "SELECT MASTER_GTID_WAIT('%s', 120)", binlog_pos.c_str());
    execute_query(test.repl->nodes[2], "STOP SLAVE");

    MYSQL* blr = open_conn_no_db(test.maxscales->binlog_port[0],
                                 test.maxscales->IP[0],
                                 test.repl->user_name,
                                 test.repl->password,
                                 test.ssl);

    // Call the callback that switches the master
    cb(blr);

    mysql_close(blr);

    // Do another batch of inserts
    for (int i = 0; i < 25; i++)
    {
        test.try_query(test.repl->nodes[2], "INSERT INTO test.t1 VALUES (%d)", i);
    }

    // Sync a slave and verify all of the data is replicated
    binlog_pos = get_row(test.repl->nodes[2], "SELECT @@gtid_binlog_pos")[0];
    execute_query(test.repl->nodes[3], "SELECT MASTER_GTID_WAIT('%s', 120)", binlog_pos.c_str());
    std::string sum = get_row(test.repl->nodes[3], "SELECT COUNT(*) FROM test.t1")[0];

    test.expect(sum == "50", "Inserted 50 rows but only %s were replicated", sum.c_str());
    execute_query(test.repl->nodes[0], "DROP TABLE test.t1");
}
