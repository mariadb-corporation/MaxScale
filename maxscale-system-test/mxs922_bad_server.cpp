/**
 * @file mxs922_bad_server.cpp MXS-922: Server removal test
 *
 */

#include "testconnections.h"

#define MONITOR_NAME "mysql-monitor"
#define SERVICE_NAME "rwsplit-service"

void add_servers(TestConnections* test)
{
    test->tprintf("Adding the servers");
    test->set_timeout(120);

    for (int i = 0; i < 4; i++)
    {
        test->maxscales->ssh_node_f(0, true, "maxadmin add server server%d " MONITOR_NAME, i + 1);
        test->maxscales->ssh_node_f(0, true, "maxadmin add server server%d " SERVICE_NAME, i + 1);
    }
    test->stop_timeout();
}

void remove_servers(TestConnections* test)
{
    test->tprintf("Remove the servers");
    test->set_timeout(120);

    for (int i = 0; i < 4; i++)
    {
        test->maxscales->ssh_node_f(0, true, "maxadmin remove server server%d " MONITOR_NAME, i + 1);
        test->maxscales->ssh_node_f(0, true, "maxadmin remove server server%d " SERVICE_NAME, i + 1);
    }
    test->stop_timeout();
}

void destroy_servers(TestConnections* test)
{
    test->tprintf("Destroy the servers");
    test->set_timeout(120);

    for (int i = 0; i < 4; i++)
    {
        test->maxscales->ssh_node_f(0, true, "maxadmin destroy server server%d", i + 1);
    }
    test->stop_timeout();
}

void do_query(TestConnections* test, bool should_fail)
{
    test->tprintf("Trying to query, expecting %s", should_fail ? "failure" : "success");
    test->set_timeout(120);

    test->maxscales->connect_maxscale(0);

    bool failed = execute_query(test->maxscales->conn_rwsplit[0], "select @@server_id") == 0;

    const char* msg = should_fail
        ? "Query was successful when failure was expected."
        : "Query failed when success was expected.";

    test->add_result(failed == should_fail, msg);
    test->maxscales->close_maxscale_connections(0);

    test->stop_timeout();
}

int main(int argc, char* argv[])
{
    TestConnections* test = new TestConnections(argc, argv);

    test->tprintf("Creating servers with bad addresses");

    for (int i = 0; i < 4; i++)
    {
        test->maxscales->ssh_node_f(0,
                                    true,
                                    "maxadmin create server server%d 3306 %s",
                                    i + 1,
                                    test->repl->IP[i]);
    }

    /** Add the servers to the monitor and service */
    add_servers(test);

    do_query(test, true);

    /** Remove and destroy servers from monitor and service */
    remove_servers(test);
    destroy_servers(test);

    test->tprintf("Create the servers with correct parameters");
    for (int i = 0; i < 4; i++)
    {
        test->maxscales->ssh_node_f(0,
                                    true,
                                    "maxadmin create server server%d %s %d",
                                    i + 1,
                                    test->repl->IP[i],
                                    test->repl->port[i]);
    }

    /**  Add the servers again */
    add_servers(test);

    test->tprintf("Wait for the monitor to see the new servers");
    sleep(2);

    do_query(test, false);

    /** Remove everything */
    remove_servers(test);
    destroy_servers(test);

    do_query(test, true);

    test->check_maxscale_processes(0, 1);
    int rval = test->global_result;
    delete test;
    return rval;
}
