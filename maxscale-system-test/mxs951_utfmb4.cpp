/**
 * @file mxs951_utfmb4_galera.cpp Set utf8mb4 in the backend and restart Maxscale
 * - add following to backend server configuration:
 @verbatim
[mysqld]
character_set_server=utf8mb4
collation_server=utf8mb4_unicode_520_ci
 @endverbatim
 * - for all backend nodes: SET GLOBAL character_set_server = 'utf8mb4'; SET NAMES 'utf8mb4'
 * - restart Maxscale
 * - connect to Maxscale
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    Test->stop_timeout();

    char cmd [1024];
    sprintf(cmd, "%s/utf64.cnf", test_dir);
    for (int i = 0; i < Test->repl->N; i++)
    {
        Test->repl->copy_to_node(cmd, (char *) "./", i);
        Test->repl->ssh_node(i, (char *) "cp ./utf64.cnf /etc/my.cnf.d/", true);
    }

    Test->repl->start_replication();


    Test->tprintf("Set utf8mb4 for backend");
    Test->repl->execute_query_all_nodes((char *) "SET GLOBAL character_set_server = 'utf8mb4';");

    Test->tprintf("Set names to utf8mb4 for backend");
    Test->repl->execute_query_all_nodes((char *) "SET NAMES 'utf8mb4';");

    Test->set_timeout(120);

    Test->tprintf("Restart Maxscale");
    Test->restart_maxscale();

    Test->check_maxscale_alive();

    Test->stop_timeout();
    Test->tprintf("Restore backend configuration\n");
    for (int i = 0; i < Test->repl->N; i++)
    {
        Test->repl->ssh_node(i, (char *) "rm  /etc/my.cnf.d/utf64.cnf", true);
    }
    Test->repl->start_replication();

    int rval = Test->global_result;
    delete Test;
    return rval;
}

