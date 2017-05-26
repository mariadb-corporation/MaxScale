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

    test.connect_maxscale();
    test.try_query(test.conn_rwsplit, "INSERT INTO test.t1 VALUES (1)");
    test.close_maxscale_connections();
}

void check_slave(TestConnections& test)
{
    test.add_result(test.find_slave_maxadmin(test.repl) == -1, "No slaves found");

    test.connect_maxscale();
    test.try_query(test.conn_rwsplit, "SELECT * FROM test.t1");
    test.close_maxscale_connections();
}

void kill_maxscale(TestConnections& test)
{
    test.tprintf("Killing and restarting MaxScale");
    test.ssh_maxscale(true, "pkill -9 maxscale");
    test.start_maxscale();

    test.tprintf("Waiting for MaxScale to start");
    sleep(10);
}

void restart_maxscale(TestConnections& test)
{
    test.restart_maxscale();
    test.tprintf("Waiting for MaxScale to start");
    sleep(10);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.connect_maxscale();
    test.try_query(test.conn_rwsplit, "CREATE OR REPLACE TABLE test.t1(id int)");
    test.close_maxscale_connections();

    test.tprintf("Checking that node 0 is the master and slaves are OK");
    check_master(test);
    check_slave(test);

    test.tprintf("Blocking slaves to trigger stale master status");
    test.repl->block_node(1);
    test.repl->block_node(2);
    test.repl->block_node(3);
    sleep(5);

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
    sleep(5);

    test.tprintf("Checking that node 0 is the master and slaves are OK");
    check_master(test);
    check_slave(test);

    test.tprintf("Blocking master to trigger stale slave status");
    test.repl->block_node(0);
    sleep(5);

    test.tprintf("Checking that slaves have stale status");
    check_slave(test);

    kill_maxscale(test);

    test.tprintf("Checking that slaves still have stale status");
    check_slave(test);

    restart_maxscale(test);

    test.tprintf("Checking that slaves have stale status after restart");
    check_slave(test);

    test.repl->unblock_node(0);
    sleep(5);

    test.tprintf("Checking that node 0 is the master and slaves are OK");
    check_master(test);
    check_slave(test);

    return test.global_result;
}
