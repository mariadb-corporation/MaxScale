/**
 * @file slave_failover.cpp  Check how Maxscale works in case of one slave failure, only one slave is
 * configured
 *
 * - Connect to RWSplit
 * - find which backend slave is used for connection
 * - blocm mariadb on the slave with firewall
 * - wait 60 seconds
 * - check which slave is used for connection now, expecting any other slave
 * - check warning in the error log about broken slave
 * - unblock mariadb backend (restore slave firewall settings)
 * - check if Maxscale still alive
 */


#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    printf("Connecting to RWSplit");
    test.set_timeout(60);
    test.add_result(test.maxscales->connect_rwsplit(0), "Error connection to RWSplit! Exiting");
    test.maxscales->wait_for_monitor();

    test.tprintf("Checking current slave");
    int res = 0;
    int old_slave = test.find_connected_slave(0, &res);
    test.add_result(res, "no current slave");

    test.tprintf("Setup firewall to block mysql on old slave (oldslave is node %d)", old_slave);

    test.add_result((old_slave < 0) || (old_slave >= test.repl->N), "Active slave is not found");
    test.repl->block_node(old_slave);

    test.tprintf("Waiting for MaxScale to find a new slave");
    test.stop_timeout();
    test.maxscales->wait_for_monitor();

    test.set_timeout(20);
    int current_slave = test.find_connected_slave(0, &res);
    test.add_result((current_slave == old_slave) || (current_slave < 0), "No failover happened");

    test.tprintf("Unblock old node");
    test.repl->unblock_node(old_slave);
    test.maxscales->close_rwsplit(0);

    test.check_maxscale_alive(0);
    test.stop_timeout();
    test.repl->fix_replication();

    return test.global_result;
}
