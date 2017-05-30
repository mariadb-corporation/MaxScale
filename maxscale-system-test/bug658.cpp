/**
 * @file bug658.cpp regression case for bug 658 ("readconnroute: client is not closed if backend fails")
 *
 * - Connect all MaxScale
 * - block Mariadb server on Master node by Firewall
 * - execute query
 * - unblock Mariadb server
 * - do same test, but block all backend nodes
 * - check if Maxscale is alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(40);
    int i;

    Test->tprintf("Connecting to Maxscale %s\n", Test->maxscale_IP);
    Test->connect_maxscale();

    printf("Setup firewall to block mysql on master\n"); fflush(stdout);
    Test->repl->block_node(0);

    sleep(1);

    Test->tprintf("Trying query to RWSplit, expecting failure, but not a crash\n"); fflush(stdout);
    execute_query(Test->conn_rwsplit, (char *) "show processlist;");fflush(stdout);
    Test->tprintf("Trying query to ReadConn master, expecting failure, but not a crash\n"); fflush(stdout);
    execute_query(Test->conn_master, (char *) "show processlist;");fflush(stdout);
    Test->tprintf("Trying query to ReadConn slave, expecting failure, but not a crash\n"); fflush(stdout);
    execute_query(Test->conn_slave, (char *) "show processlist;");fflush(stdout);

    sleep(1);

    Test->repl->unblock_node(0);
    sleep(10);

    Test->close_maxscale_connections();

    Test->tprintf("Checking Maxscale is alive\n");
    Test->check_maxscale_alive();

    Test->set_timeout(20);

    Test->tprintf("Connecting to Maxscale %s to check its behaviour in case of blocking all bacxkends\n", Test->maxscale_IP);
    Test->connect_maxscale();

    if (!Test->smoke) {
        for (i = 0; i < Test->repl->N; i++) {
            Test->tprintf("Setup firewall to block mysql on node %d\n", i);
            Test->repl->block_node(i); fflush(stdout);
        }
        sleep(1);

        Test->tprintf("Trying query to RWSplit, expecting failure, but not a crash\n");
        execute_query(Test->conn_rwsplit, (char *) "show processlist;");fflush(stdout);
        Test->tprintf("Trying query to ReadConn master, expecting failure, but not a crash\n");
        execute_query(Test->conn_master, (char *) "show processlist;");fflush(stdout);
        Test->tprintf("Trying query to ReadConn slave, expecting failure, but not a crash\n");
        execute_query(Test->conn_slave, (char *) "show processlist;");fflush(stdout);

        sleep(1);

        for (i = 0; i < Test->repl->N; i++) {
            Test->tprintf("Setup firewall back to allow mysql on node %d\n", i);
            Test->repl->unblock_node(i); fflush(stdout);
        }
    }
    Test->stop_timeout();
    Test->tprintf("Sleeping 20 seconds\n");
    sleep(20);

    Test->set_timeout(20);

    Test->close_maxscale_connections();
    Test->tprintf("Checking Maxscale is alive\n");
    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}
