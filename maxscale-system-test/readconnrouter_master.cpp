/**
 * Connect to readconnroute in master mode and check that it always connects to the master
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.set_timeout(100);

    test.repl->connect();
    test.tprintf("Connecting to ReadConnnRouter in 'master' mode");
    test.maxscales->connect_readconn_master(0);
    auto master = get_row(test.repl->nodes[0], "SELECT @@server_id");
    auto maxscale = get_row(test.maxscales->conn_master[0], "SELECT @@server_id");
    test.expect(master == maxscale, "Connection did not go to the master: %s", maxscale[0].c_str());
    test.maxscales->close_readconn_master(0);

    test.tprintf("Changing master to node 1");
    test.set_timeout(50);
    test.repl->change_master(1, 0);
    test.stop_timeout();
    test.maxscales->wait_for_monitor();

    test.tprintf("Connecting to ReadConnnRouter in 'master' mode");
    test.set_timeout(50);
    test.maxscales->connect_readconn_master(0);
    master = get_row(test.repl->nodes[1], "SELECT @@server_id");
    maxscale = get_row(test.maxscales->conn_master[0], "SELECT @@server_id");
    test.expect(master == maxscale, "Connection did not go to the master: %s", maxscale[0].c_str());
    test.maxscales->close_readconn_master(0);

    test.repl->change_master(0, 1);
    test.log_excludes(0, "The service 'CLI' is missing a definition of the servers");

    return test.global_result;
}
