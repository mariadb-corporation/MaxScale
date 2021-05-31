/**
 * @file check_backend.cpp simply checks if backend is alive
 */


#include <iostream>
#include <maxtest/testconnections.hh>

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);

    // Reset server settings by replacing the config files
    Test->repl->reset_all_servers_settings();

    Test->set_timeout(10);

    Test->tprintf("Connecting to Maxscale maxscales->routers[0] with Master/Slave backend\n");
    Test->maxscales->connect_maxscale();
    Test->tprintf("Testing connections\n");

    Test->add_result(Test->test_maxscale_connections(true, true, true), "Can't connect to backend\n");

    Test->tprintf("Connecting to Maxscale router with Galera backend\n");
    MYSQL * g_conn = open_conn(4016, Test->maxscales->ip4(), Test->maxscales->user_name, Test->maxscales->password, Test->maxscale_ssl);
    if (g_conn != NULL )
    {
        Test->tprintf("Testing connection\n");
        Test->add_result(Test->try_query(g_conn, (char *) "SELECT 1"),
                         (char *) "Error executing query against RWSplit Galera\n");
    }

    Test->tprintf("Closing connections\n");
    Test->maxscales->close_maxscale_connections();
    Test->check_maxscale_alive();

    auto ver = Test->maxscales->ssh_output("maxscale --version-full", 0, false);
    Test->tprintf("Maxscale_full_version_start:\n%s\nMaxscale_full_version_end\n", ver.output.c_str());

    int rval = Test->global_result;
    delete Test;
    return rval;
}
