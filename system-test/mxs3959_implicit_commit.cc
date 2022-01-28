#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());

    c.query("CREATE OR REPLACE TABLE test.t1(id INT)");
    c.query("BEGIN");
    c.query("INSERT INTO test.t1 VALUES (1)");
    c.query("BEGIN");
    c.query("INSERT INTO test.t1 VALUES (2)");

    // Block and unblock the master
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor(2);
    test.repl->unblock_node(0);
    test.maxscale->wait_for_monitor(2);

    c.query("COMMIT");

    auto f = c.field("SELECT COUNT(*) FROM test.t1");
    test.expect(f == "2", "The table should have 2 rows in it, not %s", f.c_str());

    c.query("DROP TABLE test.t1");

    return test.global_result;
}
