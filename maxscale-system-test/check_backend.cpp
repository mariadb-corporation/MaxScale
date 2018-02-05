/**
 * @file check_backend.cpp simply checks if backend is alive
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);

    // Reset server settings by replacing the config files
    Test->repl->reset_server_settings();

    std::string src = std::string(test_dir) + "/mdbci/add_core_cnf.sh";
    Test->maxscales->copy_to_node(0, src.c_str(), Test->maxscales->access_homedir[0]);
    Test->maxscales->ssh_node_f(0, true, "%s/add_core_cnf.sh %s", Test->maxscales->access_homedir[0],
                                Test->verbose ? "verbose" : "");

    Test->set_timeout(10);

    Test->tprintf("Connecting to Maxscale maxscales->routers[0] with Master/Slave backend\n");
    Test->maxscales->connect_maxscale(0);
    Test->tprintf("Testing connections\n");

    Test->add_result(Test->test_maxscale_connections(0, true, true, true), "Can't connect to backend\n");

    if ((Test->galera != NULL) && (Test->galera->N != 0))
    {
        Test->tprintf("Connecting to Maxscale router with Galera backend\n");
        MYSQL * g_conn = open_conn(4016 , Test->maxscales->IP[0], Test->maxscales->user_name, Test->maxscales->password, Test->ssl);
        if (g_conn != NULL )
        {
            Test->tprintf("Testing connection\n");
            Test->add_result(Test->try_query(g_conn, (char *) "SELECT 1"),
                             (char *) "Error executing query against RWSplit Galera\n");
        }
    }
    else
    {
        Test->tprintf("Galera is not in use\n");
    }
    Test->tprintf("Closing connections\n");
    Test->maxscales->close_maxscale_connections(0);
    Test->check_maxscale_alive(0);

    int exit_code = 0;
    char * ver = Test->maxscales->ssh_node_output(0, "maxscale --version-full", false, &exit_code);
    Test->tprintf("Maxscale_full_version_start:\n%s\nMaxscale_full_version_end\n", ver);

    if ((Test->global_result == 0) && (Test->use_snapshots))
    {
        Test->tprintf("Taking snapshot\n");
        Test->take_snapshot((char *) "clean");
    }
    else
    {
        Test->tprintf("Snapshots are not in use\n");
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}
