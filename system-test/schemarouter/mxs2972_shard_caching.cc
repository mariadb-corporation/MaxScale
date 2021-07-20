#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect();
    test.try_query(test.repl->nodes[0], "CREATE DATABASE db1");
    test.try_query(test.repl->nodes[0], "CREATE OR REPLACE TABLE test.t1(id INT)");
    test.repl->disconnect();

    auto conn = test.maxscale->rwsplit();
    conn.set_database("db1");

    test.tprintf("Block server1 and perform a simple SELECT");
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor();
    test.expect(conn.connect(), "Connection should work: %s", conn.error());

    auto db = conn.field("SELECT DATABASE()");
    test.expect(db == "db1", "Database should be `db1`: %s", db.c_str());

    test.expect(conn.query("SELECT 1"), "Query should work: %s", conn.error());

    conn.disconnect();
    test.repl->unblock_node(0);
    test.maxscale->wait_for_monitor();

    test.tprintf("Unblock server1 and perform a DELETE that is forced to server1");
    test.expect(conn.connect(), "Connection should work: %s", conn.error());

    db = conn.field("SELECT DATABASE()");
    test.expect(db == "db1", "Database should be `db1`: %s", db.c_str());

    test.expect(conn.query("DELETE t FROM test.t1 AS t"), "Query should work: %s", conn.error());

    conn.disconnect();

    test.repl->connect();
    test.try_query(test.repl->nodes[0], "DROP DATABASE db1");
    test.try_query(test.repl->nodes[0], "DROP TABLE test.t1");
    test.repl->disconnect();

    return test.global_result;
}
