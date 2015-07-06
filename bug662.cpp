/**
 * @file bug662.cpp regression case for bug 662 ("MaxScale hangs in startup if backend server is not responsive"), covers also bug680 ("RWSplit can't load DB user if backend is not available at MaxScale start")
 *
 * - block all Mariadb servers  Firewall
 * - restart MaxScale
 * - check it took no more then 20 seconds
 * - unblock Mariadb servers
 * - sleep one minute
 * - check if Maxscale is alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int i;

    Test->read_env();
    Test->print_env();

    printf("Connecting to Maxscale %s\n", Test->maxscale_IP);

    char sys1[4096];

    printf("Connecting to Maxscale %s to check its behaviour in case of blocking all bacxkends\n", Test->maxscale_IP);
    Test->connect_maxscale();

    for (i = 0; i < Test->repl->N; i++) {
        printf("Setup firewall to block mysql on node %d\n", i); fflush(stdout);
        Test->repl->block_node(i); fflush(stdout);
    }

    pid_t pid = fork();
    if (!pid) {
        Test->restart_maxscale(); fflush(stdout);
    } else {

        printf("Waiting 20 seconds\n"); fflush(stdout);
        sleep(20);

        printf("Checking if MaxScale is alive by connecting to MaxAdmin\n"); fflush(stdout);
        global_result += execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char* ) "show servers");

        for (i = 0; i < Test->repl->N; i++) {
            printf("Setup firewall back to allow mysql on node %d\n", i); fflush(stdout);
            Test->repl->unblock_node(i);fflush(stdout);
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
}
