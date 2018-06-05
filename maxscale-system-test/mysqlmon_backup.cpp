/**
 * MySQL Monitor crash safety test
 *
 * - Start MaxScale
 * - Kill slaves to trigger stale master status
 * - Kill MaxScale process and restart MaxScale
 * - Expect stale master status to still exist for the master
 */

#include "testconnections.h"

void check_master(TestConnections& test)
{
    test.add_result(test.find_master_maxadmin(test.repl) != 0, "Node 0 is not the master");

    test.maxscales->connect_maxscale(0);
    test.try_query(test.maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");
    test.maxscales->close_maxscale_connections(0);
}

void check_slave(TestConnections& test)
{
    test.add_result(test.find_slave_maxadmin(test.repl) == -1, "No slaves found");

    test.maxscales->connect_maxscale(0);
    test.try_query(test.maxscales->conn_rwsplit[0], "SELECT * FROM test.t1");
    test.maxscales->close_maxscale_connections(0);
}

void kill_maxscale(TestConnections& test)
{
    test.tprintf("Killing and restarting MaxScale");
    test.maxscales->ssh_node_f(0, true, "pkill -9 maxscale");
    test.maxscales->start_maxscale(0);

    test.tprintf("Waiting for MaxScale to start");
    sleep(10);
}

void restart_maxscale(TestConnections& test)
{
    test.maxscales->restart_maxscale(0);
    test.tprintf("Waiting for MaxScale to start");
    sleep(10);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscales->connect_maxscale(0);
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1(id int)");
    test.maxscales->close_maxscale_connections(0);

    test.tprintf("Checking that node 0 is the master and slaves are OK");
    check_master(test);
    check_slave(test);

    test.tprintf("Blocking slaves to trigger stale master status");
    test.repl->block_node(1);
    test.repl->block_node(2);
    test.repl->block_node(3);
    sleep(10);

    test.tprintf("Checking that master has stale status");
    check_master(test);

    kill_maxscale(test);

    test.tprintf("Checking that master still has stale status");
    check_master(test);

    restart_maxscale(test);

    test.tprintf("Checking that master has stale status after restart");
    check_master(test);

    test.repl->unblock_node(1);
    test.repl->unblock_node(2);
    test.repl->unblock_node(3);
    sleep(10);

    test.tprintf("Checking that node 0 is the master and slaves are OK");
    check_master(test);
    check_slave(test);

    test.tprintf("Blocking master to trigger stale slave status");
    test.repl->block_node(0);
    sleep(10);

    test.tprintf("Checking that slaves have stale status");
    check_slave(test);

    kill_maxscale(test);

    test.tprintf("Checking that slaves still have stale status");
    check_slave(test);

    restart_maxscale(test);

    test.tprintf("Checking that slaves have stale status after restart");
    check_slave(test);

    test.repl->unblock_node(0);
    sleep(10);

    test.tprintf("Checking that node 0 is the master and slaves are OK");
    check_master(test);
    check_slave(test);

    return test.global_result;
}
