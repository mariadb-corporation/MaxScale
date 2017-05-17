/**
 * @file bug539.cpp  regression case for bug539 ("MaxScale crashes in session_setup_filters")
 * using maxadmin execute "fail backendfd"
 * try quries against all services
 * using maxadmin execute "fail clientfd"
 * try quries against all services
 * check if MaxScale alive
 */


#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);
    int i, j;
    MYSQL * conn;

    int N_cmd = 2;
    char * fail_cmd[N_cmd - 1];

    int N_ports = 3;
    int ports[N_ports];

    fail_cmd[0] = (char *) "fail backendfd";
    fail_cmd[1] = (char *) "fail clientfd";

    ports[0] = Test->rwsplit_port;
    ports[1] = Test->readconn_master_port;
    ports[2] = Test->readconn_slave_port;

    for (i = 0; i < N_cmd; i++)
    {
        for (j = 0; j < N_ports; j++)
        {
            Test->tprintf("Executing MaxAdmin command '%s'\n", fail_cmd[i]);
            if (execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, fail_cmd[i]) != 0)
            {
                Test->add_result(1, "MaxAdmin command failed\n");
            }
            else
            {
                printf("Trying query against %d\n", ports[j]);
                conn = open_conn(ports[j], Test->maxscale_IP, Test->maxscale_user, Test->maxscale_user, Test->ssl);
                Test->try_query(conn, (char *) "show processlist;");
            }
        }
    }

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
