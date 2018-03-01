/**
 * @file check_backend.cpp simply checks if backend is alive
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);

    std::string src = std::string(test_dir) + "/mdbci/add_core_cnf.sh";
    Test->copy_to_maxscale(src.c_str(), Test->maxscale_access_homedir);
    Test->ssh_maxscale(true, "%s/add_core_cnf.sh %s", Test->maxscale_access_homedir,
                       Test->verbose ? "verbose" : "");

    Test->set_timeout(10);

    Test->tprintf("Connecting to Maxscale routers with Master/Slave backend\n");
    Test->connect_maxscale();
    Test->tprintf("Testing connections\n");
    Test->add_result(Test->test_maxscale_connections(true, true, true), "Can't connect to backend\n");

    if ((Test->galera != NULL) && (Test->galera->N != 0))
    {
        Test->tprintf("Connecting to Maxscale router with Galera backend\n");
        MYSQL * g_conn = open_conn(4016 , Test->maxscale_IP, Test->maxscale_user, Test->maxscale_password, Test->ssl);
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
    Test->close_maxscale_connections();
    Test->check_maxscale_alive();

    char * ver = Test->ssh_maxscale_output(false, "maxscale --version-full");
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
