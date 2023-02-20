#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.repl->execute_query_all_nodes("STOP SLAVE");
    auto node = test.repl->get_connection(0);

    test.tprintf("Create tables t1 and T1: they should be treated as the same table");

    test.expect(node.connect(), "Failed to connect: %s", node.error());
    test.expect(node.query("CREATE TABLE test.t1(id INT)"),
                "Failed to create `test` . `t1`: %s", node.error());
    test.expect(node.query("CREATE TABLE test.T1(id INT)"),
                "Failed to create `test` . `T1`: %s", node.error());

    auto rws = test.maxscale->rwsplit();
    test.expect(rws.connect(), "Failed to connect to readwritesplit: %s", rws.error());
    test.expect(rws.query("SELECT * FROM test.t1"), "Failed to query `test` . `t1`: %s", rws.error());
    test.expect(rws.query("SELECT * FROM test.T1"), "Failed to query `test` . `T1`: %s", rws.error());

    node.query("DROP TABLE test.t1");
    node.query("DROP TABLE test.T1");

    return test.global_result;
}
