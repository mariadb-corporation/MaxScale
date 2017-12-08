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




    fail_cmd[0] = (char *) "fail backendfd";
    fail_cmd[1] = (char *) "fail clientfd";


    for (i = 0; i < N_cmd; i++)
    {
        for (j = 0; j < Test->maxscales->N_ports[0]; j++)
        {
            Test->tprintf("Executing MaxAdmin command '%s'\n", fail_cmd[i]);
            if (maxscales->execute_maxadmin_command(0, Test->maxscales->IP[0], (char *) "admin", Test->maxscales->maxadmin_password[0], fail_cmd[i]) != 0)
            {
                Test->add_result(1, "MaxAdmin command failed\n");
            }
            else
            {
                printf("Trying query against %d\n", Test->maxscales->ports[0][j]);
                conn = open_conn(ports[j], Test->maxscales->IP[0], Test->maxscales->user_name, Test->maxscales->password, Test->ssl);
                Test->try_query(conn, (char *) "show processlist;");
            }
        }
    }

    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
