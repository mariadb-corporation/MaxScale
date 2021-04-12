#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    const int N_ROWS = 10;
    TestConnections test(argc, argv);

    auto conn = test.maxscales->rwsplit();
    auto node1 = test.repl->get_connection(0);
    auto node2 = test.repl->get_connection(1);

    test.expect(conn.connect(), "Readwritesplit connection failed: %s", conn.error());
    test.expect(node1.connect(), "Node 1 connection failed: %s", node1.error());
    test.expect(node2.connect(), "Node 2 connection failed: %s", node2.error());

    node2.query("STOP SLAVE");

    test.tprintf("Creating table on node 1 and 2");
    node1.query("CREATE TABLE test.t1(id INT)");
    node2.query("CREATE TABLE test.t1(id INT)");

    test.tprintf("Lock the table on node 2 so that writes are blocked");
    node2.query("LOCK TABLE test.t1 WRITE");

    test.tprintf("Insert %d rows into the table", N_ROWS);
    for (int i = 0; i < N_ROWS; i++)
    {
        conn.query("INSERT INTO test.t1 VALUES (1)");
    }

    test.tprintf("Disconnect from MaxScale");
    conn.disconnect();

    auto res1 = node1.field("SELECT COUNT(*) FROM test.t1");
    auto res2 = node2.field("SELECT COUNT(*) FROM test.t1");

    test.tprintf("Node 1: %s rows Node 2: %s rows", res1.c_str(), res2.c_str());
    test.expect(res1 != res2, "Node 1 should have more rows");

    test.tprintf("Unlock the table and wait for the inserts to complete");
    node2.query("UNLOCK TABLES");
    sleep(5);

    res1 = node1.field("SELECT COUNT(*) FROM test.t1");
    res2 = node2.field("SELECT COUNT(*) FROM test.t1");

    test.tprintf("Node 1: %s rows Node 2: %s rows", res1.c_str(), res2.c_str());
    test.expect(res1 == res2, "Both should have the same amount of rows");

    return test.global_result;
}
