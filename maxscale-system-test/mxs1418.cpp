/**
 * @file Check that removing a server from a service doesn't break active connections
 */

#include "testconnections.h"

static volatile bool running = true;

void* thr(void* data)
{
    TestConnections* test = (TestConnections*)data;

    while (running && test->global_result == 0)
    {
        test->set_timeout(60);
        if (test->try_query(test->conn_rwsplit, "SELECT 1"))
        {
            test->tprintf("Failed to select via readwritesplit");
        }
        if (test->try_query(test->conn_master, "SELECT 1"))
        {
            test->tprintf("Failed to select via readconnroute master");
        }
        if (test->try_query(test->conn_slave, "SELECT 1"))
        {
            test->tprintf("Failed to select via readconnroute slave");
        }
    }

    test->stop_timeout();

    return NULL;
}

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);
    test.connect_maxscale();

    test.tprintf("Connect to MaxScale and continuously execute queries");
    pthread_t thread;
    pthread_create(&thread, NULL, thr, &test);
    sleep(5);

    test.tprintf("Remove all servers from all services");

    for (int i = 3; i > -1; i--)
    {
        test.ssh_maxscale(true, "maxadmin remove server server%d \"RW Split Router\"", i);
        test.ssh_maxscale(true, "maxadmin remove server server%d \"Read Connection Router Slave\"", i);
        test.ssh_maxscale(true, "maxadmin remove server server%d \"Read Connection Router Master\"", i);
    }

    sleep(5);

    test.tprintf("Stop queries and close the connections");
    running = false;
    pthread_join(thread, NULL);
    test.close_maxscale_connections();

    test.tprintf("Add all servers to all services");

    for (int i = 3; i > -1; i--)
    {
        test.ssh_maxscale(true, "maxadmin add server server%d \"RW Split Router\"", i);
        test.ssh_maxscale(true, "maxadmin add server server%d \"Read Connection Router Slave\"", i);
        test.ssh_maxscale(true, "maxadmin add server server%d \"Read Connection Router Master\"", i);
    }

    test.check_maxscale_alive();

    return test.global_result;
}
