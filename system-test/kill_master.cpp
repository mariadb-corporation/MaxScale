/**
 * @file kill_master.cpp Checks Maxscale behaviour in case if Master node is blocked
 *
 * - Connect to RWSplit
 * - block Mariadb server on Master node by Firewall
 * - try simple query *show processlist" expecting failure, but not a crash
 * - check if Maxscale is alive
 * - reconnect and check if query execution is ok
 */


#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->reset_timeout();

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscale->ip4());
    Test->maxscale->connect_rwsplit();

    Test->reset_timeout();
    Test->tprintf("Setup firewall to block mysql on master\n");
    Test->repl->block_node(0);

    Test->tprintf("Trying query to RWSplit, expecting failure, but not a crash\n");
    Test->reset_timeout();
    execute_query(Test->maxscale->conn_rwsplit[0], (char*) "show processlist;");

    Test->reset_timeout();
    Test->tprintf("Setup firewall back to allow mysql\n");
    Test->repl->unblock_node(0);

    Test->maxscale->wait_for_monitor();

    Test->reset_timeout();
    Test->tprintf("Reconnecting and trying query to RWSplit\n");
    Test->maxscale->connect_rwsplit();
    Test->try_query(Test->maxscale->conn_rwsplit[0], (char*) "show processlist;");
    Test->maxscale->close_rwsplit();

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
