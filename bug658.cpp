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
    int global_result = 0;
    int i;

    Test->read_env();
    Test->print_env();

    printf("Connecting to Maxscale %s\n", Test->maxscale_IP);
    Test->connect_maxscale();


    char sys1[4096];


    printf("Setup firewall to block mysql on master\n"); fflush(stdout);
    Test->repl->block_node(0);

    sleep(1);

    printf("Trying query to RWSplit, expecting failure, but not a crash\n"); fflush(stdout);
    execute_query(Test->conn_rwsplit, (char *) "show processlist;");fflush(stdout);
    printf("Trying query to ReadConn master, expecting failure, but not a crash\n"); fflush(stdout);
    execute_query(Test->conn_master, (char *) "show processlist;");fflush(stdout);
    printf("Trying query to ReadConn slave, expecting failure, but not a crash\n"); fflush(stdout);
    execute_query(Test->conn_slave, (char *) "show processlist;");fflush(stdout);

    sleep(1);


    Test->repl->unblock_node(0);
    sleep(10);

    printf("Checking Maxscale is alive\n"); fflush(stdout);
    global_result += check_maxscale_alive(); fflush(stdout);
    if (global_result !=0) {
        printf("MaxScale is not alive\n");fflush(stdout);
    } else {
        printf("MaxScale is still alive\n");fflush(stdout);
    }

    Test->close_maxscale_connections(); fflush(stdout);

    printf("Connecting to Maxscale %s to check its behaviour in case of blocking all bacxkends\n", Test->maxscale_IP);
    Test->connect_maxscale();

    for (i = 0; i < Test->repl->N; i++) {
        printf("Setup firewall to block mysql on node %d\n", i); fflush(stdout);
        Test->repl->block_node(i); fflush(stdout);
    }
    sleep(1);

    printf("Trying query to RWSplit, expecting failure, but not a crash\n"); fflush(stdout);
    execute_query(Test->conn_rwsplit, (char *) "show processlist;");fflush(stdout);
    printf("Trying query to ReadConn master, expecting failure, but not a crash\n"); fflush(stdout);
    execute_query(Test->conn_master, (char *) "show processlist;");fflush(stdout);
    printf("Trying query to ReadConn slave, expecting failure, but not a crash\n"); fflush(stdout);
    execute_query(Test->conn_slave, (char *) "show processlist;");fflush(stdout);

    sleep(1);

    for (i = 0; i < Test->repl->N; i++) {
        printf("Setup firewall back to allow mysql on node %d\n", i); fflush(stdout);
        Test->repl->unblock_node(i); fflush(stdout);
    }

    printf("Sleeping 60 seconds\n"); fflush(stdout);
    sleep(60);

    printf("Checking Maxscale is alive\n"); fflush(stdout);
    global_result += check_maxscale_alive(); fflush(stdout);
    if (global_result !=0) {
        printf("MaxScale is not alive\n");
    } else {
        printf("MaxScale is still alive\n");
    }


    Test->close_maxscale_connections(); fflush(stdout);


    printf("Reconnecting and trying query to RWSplit\n"); fflush(stdout);
    Test->connect_maxscale();
    global_result += execute_query(Test->conn_rwsplit, (char *) "show processlist;");
    printf("Trying query to ReadConn master\n"); fflush(stdout);
    global_result += execute_query(Test->conn_master, (char *) "show processlist;");
    printf("Trying query to ReadConn slave\n"); fflush(stdout);
    global_result += execute_query(Test->conn_slave, (char *) "show processlist;");
    Test->close_maxscale_connections();

    Test->copy_all_logs(); return(global_result);
}

