/**
 * MXS-1743: Maxscale unable to enforce round-robin between read service for Slave
 *
 * https://jira.mariadb.org/browse/MXS-1743
 */
#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);


    test.tprintf("Testing with both master and slave up");
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_master[0], "SELECT 1");
    test.maxscales->disconnect();

    test.tprintf("Testing with only the master");
    test.repl->block_node(0);
    sleep(5);
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_master[0], "SELECT 1");
    test.maxscales->disconnect();
    test.repl->unblock_node(0);
    sleep(5);

    test.tprintf("Testing with only the slave");
    test.repl->block_node(1);
    sleep(5);
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_master[0], "SELECT 1");
    test.maxscales->disconnect();
    test.repl->unblock_node(1);

    return test.global_result;
}
