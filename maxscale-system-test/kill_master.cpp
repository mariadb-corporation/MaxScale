/**
 * @file kill_master.cpp Checks Maxscale behaviour in case if Master node is blocked
 *
 * - Connect to RWSplit
 * - block Mariadb server on Master node by Firewall
 * - try simple query *show processlist" expecting failure, but not a crash
 * - check if Maxscale is alive
 * - reconnect and check if query execution is ok
 */


#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscale_IP);
    Test->connect_rwsplit();

    Test->set_timeout(30);
    Test->tprintf("Setup firewall to block mysql on master\n");
    Test->repl->block_node(0);

    Test->tprintf("Trying query to RWSplit, expecting failure, but not a crash\n");
    Test->set_timeout(30);
    execute_query(Test->conn_rwsplit, (char *) "show processlist;");

    Test->set_timeout(30);
    Test->tprintf("Setup firewall back to allow mysql\n");
    Test->repl->unblock_node(0);

    Test->stop_timeout();
    sleep(10);

    Test->set_timeout(30);
    Test->tprintf("Reconnecting and trying query to RWSplit\n");
    Test->connect_rwsplit();
    Test->try_query(Test->conn_rwsplit, (char *) "show processlist;");
    Test->close_rwsplit();

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}

