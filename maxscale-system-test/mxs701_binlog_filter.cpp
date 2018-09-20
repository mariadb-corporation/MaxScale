/**
 * MXS-701: Binlog filtering
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.start_binlog();

    test.repl->connect();
    execute_query_silent(test.repl->nodes[0], "DROP DATABASE a");
    execute_query_silent(test.repl->nodes[0], "DROP DATABASE b");
    execute_query(test.repl->nodes[0], "CREATE DATABASE a");
    execute_query(test.repl->nodes[0], "CREATE DATABASE b");
    execute_query(test.repl->nodes[0], "CREATE TABLE a.t1(id INT)");
    execute_query(test.repl->nodes[0], "CREATE TABLE a.t2(id INT)");
    execute_query(test.repl->nodes[0], "CREATE TABLE b.t2(id INT)");
    execute_query(test.repl->nodes[0], "INSERT INTO a.t1 VALUES (1)");
    execute_query(test.repl->nodes[0], "INSERT INTO a.t2 VALUES (2)");
    execute_query(test.repl->nodes[0], "INSERT INTO b.t2 VALUES (3)");

    Row gtid = get_row(test.repl->nodes[0], "SELECT @@gtid_current_pos");
    test.tprintf("Synchronizing slaves on GTID %s", gtid[0].c_str());
    execute_query(test.repl->nodes[1], "SELECT MASTER_GTID_WAIT('%s')", gtid[0].c_str());
    execute_query(test.repl->nodes[2], "SELECT MASTER_GTID_WAIT('%s')", gtid[0].c_str());

    test.tprintf("Checking normal slave");
    // The first slave has no filtering
    Row a = get_row(test.repl->nodes[1], "SELECT * FROM a.t1");
    Row b = get_row(test.repl->nodes[1], "SELECT * FROM a.t2");
    Row c = get_row(test.repl->nodes[1], "SELECT * FROM b.t2");

    test.expect(!a.empty() && a[0] == "1", "a.t1 should return 1");
    test.expect(!b.empty() && b[0] == "2", "a.t2 should return 2");
    test.expect(!c.empty() && c[0] == "3", "b.t2 should return 3");

    test.tprintf("Checking filtered slave");
    //  The second slave has match=/a[.]/ and exclude=/[.]t1/
    a = get_row(test.repl->nodes[2], "SELECT * FROM a.t1");
    b = get_row(test.repl->nodes[2], "SELECT * FROM a.t2");
    c = get_row(test.repl->nodes[2], "SELECT * FROM b.t2");

    test.expect(a.empty(), "a.t1 should be empty");
    test.expect(!b.empty() && b[0] == "2", "a.t2 should return 2");
    test.expect(c.empty(), "b.t2 should be empty");

    execute_query(test.repl->nodes[0], "DROP DATABASE a");
    execute_query(test.repl->nodes[0], "DROP DATABASE b");

    test.repl->disconnect();
    test.repl->fix_replication();

    return test.global_result;
}
