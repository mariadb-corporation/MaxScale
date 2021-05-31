/**
 * MXS-2939: Test that session commands trigger a reconnection
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscale->connect_rwsplit();

    // Make sure we have at least one fully opened connection
    test.try_query(test.maxscale->conn_rwsplit[0], "select 1");

    // Block and unblock all nodes to sever all connections
    for (int i = 0; i < test.repl->N; i++)
    {
        test.repl->block_node(i);
    }

    test.maxscale->wait_for_monitor();

    test.repl->unblock_all_nodes();
    test.maxscale->wait_for_monitor();

    // Make sure that session commands trigger a reconnection if there are no open connections
    test.set_timeout(20);
    test.try_query(test.maxscale->conn_rwsplit[0], "set @a = 1");
    test.maxscale->disconnect();

    return test.global_result;
}
