/**
 * MXS-701: Binlog filtering
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    // Configures nodes[1] to replicate from nodes[0] and nodes[2] and nodes[3]
    // to replicate from the binlogrouter
    test.start_binlog();

    test.repl->connect();
    execute_query_silent(test.repl->nodes[0], "DROP DATABASE a");
    execute_query_silent(test.repl->nodes[0], "DROP DATABASE b");
    execute_query(test.repl->nodes[0], "CREATE DATABASE a");
    execute_query(test.repl->nodes[0], "CREATE DATABASE b");
    execute_query(test.repl->nodes[0], "CREATE TABLE a.t1(id INT)");
    execute_query(test.repl->nodes[0], "CREATE TABLE a.t2(id INT)");
    execute_query(test.repl->nodes[0], "CREATE TABLE b.t2(id INT)");
    execute_query(test.repl->nodes[0], "CREATE TABLE a.t3(id INT)");
    execute_query(test.repl->nodes[0], "CREATE TABLE b.t3(id INT)");
    execute_query(test.repl->nodes[0], "INSERT INTO a.t1 VALUES (1)");
    execute_query(test.repl->nodes[0], "INSERT INTO a.t2 VALUES (2)");
    execute_query(test.repl->nodes[0], "INSERT INTO b.t2 VALUES (3)");

    // Queries with default databases
    execute_query(test.repl->nodes[0], "USE a");
    execute_query(test.repl->nodes[0], "INSERT INTO t3 VALUES (1)");
    execute_query(test.repl->nodes[0], "USE b");
    execute_query(test.repl->nodes[0], "INSERT INTO t3 VALUES (2)");

    // Test parsing of query events (DDLs are always query events, never row events)
    execute_query(test.repl->nodes[0], "USE a");
    execute_query(test.repl->nodes[0], "CREATE TABLE t4 AS SELECT 1 AS `id`");
    execute_query(test.repl->nodes[0], "USE b");
    execute_query(test.repl->nodes[0], "CREATE TABLE t4 AS SELECT 2 AS `id`");

    // Let the events replicate to the slaves
    sleep(10);

    test.tprintf("Checking normal slave");
    // The first slave has no filtering
    Row a = get_row(test.repl->nodes[1], "SELECT * FROM a.t1");
    Row b = get_row(test.repl->nodes[1], "SELECT * FROM a.t2");
    Row c = get_row(test.repl->nodes[1], "SELECT * FROM b.t2");
    Row d = get_row(test.repl->nodes[1], "SELECT * FROM a.t3");
    Row e = get_row(test.repl->nodes[1], "SELECT * FROM b.t3");
    Row f = get_row(test.repl->nodes[1], "SELECT * FROM a.t4");
    Row g = get_row(test.repl->nodes[1], "SELECT * FROM b.t4");

    test.expect(!a.empty() && a[0] == "1", "a.t1 should return 1");
    test.expect(!b.empty() && b[0] == "2", "a.t2 should return 2");
    test.expect(!c.empty() && c[0] == "3", "b.t2 should return 3");
    test.expect(!d.empty() && d[0] == "1", "a.t3 should return 1");
    test.expect(!e.empty() && e[0] == "2", "b.t3 should return 2");
    test.expect(!f.empty() && f[0] == "1", "a.t4 should return 1");
    test.expect(!g.empty() && g[0] == "2", "b.t4 should return 2");

    test.tprintf("Checking filtered slave");
    //  The second slave has match=/a[.]/ and exclude=/[.]t1/
    a = get_row(test.repl->nodes[2], "SELECT * FROM a.t1");
    b = get_row(test.repl->nodes[2], "SELECT * FROM a.t2");
    c = get_row(test.repl->nodes[2], "SELECT * FROM b.t2");
    d = get_row(test.repl->nodes[2], "SELECT * FROM a.t3");
    e = get_row(test.repl->nodes[2], "SELECT * FROM b.t3");
    f = get_row(test.repl->nodes[2], "SELECT * FROM a.t4");
    g = get_row(test.repl->nodes[2], "SELECT * FROM b.t4");

    test.expect(a.empty(), "a.t1 should be empty");
    test.expect(!b.empty() && b[0] == "2", "a.t2 should return 2");
    test.expect(c.empty(), "b.t2 should be empty");
    test.expect(!d.empty() && d[0] == "1", "a.t3 should return 1");
    test.expect(e.empty(), "b.t3 should be empty");
    test.expect(!f.empty() && f[0] == "1", "a.t4 should return 1");
    test.expect(g.empty(), "b.t4 should be empty");

    execute_query(test.repl->nodes[0], "DROP DATABASE a");
    execute_query(test.repl->nodes[0], "DROP DATABASE b");

    test.repl->disconnect();
    test.repl->fix_replication();

    return test.global_result;
}
