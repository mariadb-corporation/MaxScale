/**
 * @file check_backend.cpp simply checks if backend is alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->tprintf("Connecting to Maxscale routers with Master/Slave backend\n");
    Test->connect_maxscale();
    Test->tprintf("Testing connections\n");
    Test->add_result(Test->test_maxscale_connections(true, true, true), "Can't connect to backend\n");
    Test->tprintf("Connecting to Maxscale router with Galera backend\n");
    MYSQL * g_conn = open_conn(4016 , Test->maxscale_IP, Test->maxscale_user, Test->maxscale_password, Test->ssl);
    if (g_conn != NULL )
    {
        Test->tprintf("Testing connection\n");
        Test->add_result(Test->try_query(g_conn, (char *) "SELECT 1"), (char *) "Error executing query against RWSplit Galera\n");
    }
    Test->tprintf("Closing connections\n");
    Test->close_maxscale_connections();
    Test->check_maxscale_alive();

    Test->tprintf("Maxscale_full_version_start:");
    Test->ssh_maxscale(FALSE, "maxscale --version-full");
    Test->tprintf("Maxscale_full_version_end:");

    Test->copy_all_logs(); return(Test->global_result);
}
