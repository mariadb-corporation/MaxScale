/**
 * @file check_backend.cpp simply checks if backend is alive
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    /*Test->restart_maxscale();
    sleep(5);*/
    Test->set_timeout(10);

    Test->tprintf("Connecting to Maxscale maxscales->routers[0] with Master/Slave backend\n");
    Test->connect_maxscale();
    Test->tprintf("Testing connections\n");
    Test->add_result(Test->test_maxscale_connections(true, true, true), "Can't connect to backend\n");
    Test->tprintf("Connecting to Maxscale router with Galera backend\n");
    MYSQL * g_conn = open_conn(4016 , Test->maxscales->IP[0], Test->maxscales->user_name, Test->maxscales->password, Test->ssl);
    if (g_conn != NULL )
    {
        Test->tprintf("Testing connection\n");
        Test->add_result(Test->try_query(g_conn, (char *) "SELECT 1"),
                         (char *) "Error executing query against RWSplit Galera\n");
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
