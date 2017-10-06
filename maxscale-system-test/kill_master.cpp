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

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscales->IP[0]);
    Test->maxscales->connect_rwsplit(0);

    Test->set_timeout(30);
    Test->tprintf("Setup firewall to block mysql on master\n");
    Test->repl->block_node(0);

    Test->tprintf("Trying query to RWSplit, expecting failure, but not a crash\n");
    Test->set_timeout(30);
    execute_query(Test->maxscales->conn_rwsplit[0], (char *) "show processlist;");

    Test->set_timeout(30);
    Test->tprintf("Setup firewall back to allow mysql\n");
    Test->repl->unblock_node(0);

    Test->stop_timeout();
    sleep(10);

    Test->set_timeout(30);
    Test->tprintf("Reconnecting and trying query to RWSplit\n");
    Test->maxscales->connect_rwsplit(0);
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "show processlist;");
    Test->maxscales->close_rwsplit(0);

    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}

