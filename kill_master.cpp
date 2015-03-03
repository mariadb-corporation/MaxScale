/**
 * @file kill_master.cpp Checks Maxscale behaviour in case if Master node is blocked
 *
 * - Connecto RWSplit
 * - block Mariadb server on Master node by Firewall
 * - try simple query *show processlist" expecting failure, but not a crash
 * - check if Maxscale is alive
 * - reconnect and check if query execution is ok
 */

#include <my_config.h>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->read_env();
    Test->print_env();

    printf("Connecting to RWSplit %s\n", Test->maxscale_IP);
    Test->connect_rwsplit();

    printf("Setup firewall to block mysql on master\n"); fflush(stdout);
    Test->repl->block_node(0);

    printf("Trying query to RWSplit, expecting failure, but not a crash\n"); fflush(stdout);
    execute_query(Test->conn_rwsplit, (char *) "show processlist;");

    printf("Setup firewall back to allow mysql\n"); fflush(stdout);
    Test->repl->unblock_node(0);

    sleep(10);

    global_result += check_maxscale_alive();

    Test->close_rwsplit();


    printf("Reconnecting and trying query to RWSplit\n"); fflush(stdout);
    Test->connect_rwsplit();
    global_result += execute_query(Test->conn_rwsplit, (char *) "show processlist;");
    Test->close_rwsplit();

    Test->copy_all_logs(); return(global_result);
}

