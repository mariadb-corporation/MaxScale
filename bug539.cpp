/**
 * @file bug539.cpp  regression case for bug539 ("MaxScale crashes in session_setup_filters")
 * using maxadmin execute "fail backendfd"
 * try quries against all services
 * using maxadmin execute "fail clientfd"
 * try quries against all services
 * check if MaxScale alive
 */

#include <my_config.h>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int i, j;
    char result[1024];
    MYSQL * conn;

    int N_cmd=2;
    char * fail_cmd[N_cmd-1];

    int N_ports = 3;
    int ports[N_ports];

    fail_cmd[0] = (char *) "fail backendfd";
    fail_cmd[1] = (char *) "fail clientfd";

    ports[0] = Test->rwsplit_port;
    ports[1] = Test->readconn_master_port;
    ports[2] = Test->readconn_slave_port;


    Test->read_env();
    Test->print_env();

    for (i = 0; i < N_cmd; i++) {
        for (j = 0; j < N_ports; j++) {
            printf("Executing MaxAdmin command '%s'\n", fail_cmd[i]); fflush(stdout);
            if (executeMaxadminCommand(Test->maxscale_IP, (char *) "admin", (char *) "skysql", fail_cmd[i]) != 0) {
                printf("MaxAdmin command failed\n"); fflush(stdout);
                global_result++;
            } else {
                printf("Trying query against %d\n", ports[j]);
                conn = open_conn(ports[j], Test->maxscale_IP, Test->maxscale_user, Test->maxscale_user);
                global_result += execute_query(conn, (char *) "show processlist;");
            }
        }
    }

    global_result += check_maxscale_alive();
    Test->copy_all_logs(); return(global_result);
}
