/**
 * @file mxs922_restart.cpp MXS-922: Test persisting of configuration changes
 *
 */

#include "testconnections.h"

#define MONITOR_NAME "mysql-monitor"
#define SERVICE_NAME1 "rwsplit-service"
#define SERVICE_NAME2 "read-connection-router-slave"
#define SERVICE_NAME3 "read-connection-router-master"

void add_servers(TestConnections *test)
{
    test->tprintf("Adding the servers");

    for (int i = 0; i < 4; i++)
    {
        test->set_timeout(120);
        test->ssh_maxscale(true, "maxadmin add server server%d " MONITOR_NAME, i + 1);
        test->ssh_maxscale(true, "maxadmin add server server%d " SERVICE_NAME1, i + 1);
        test->ssh_maxscale(true, "maxadmin add server server%d " SERVICE_NAME2, i + 1);
        test->ssh_maxscale(true, "maxadmin add server server%d " SERVICE_NAME3, i + 1);
        test->stop_timeout();
    }
}

void do_query(TestConnections *test, bool should_fail)
{
    test->tprintf("Trying to query, expecting %s", should_fail ? "failure" : "success");
    test->set_timeout(120);

    test->connect_maxscale();

    bool failed = execute_query(test->conn_rwsplit, "select @@server_id") == 0;

    const char *msg = should_fail ?
                      "Query was successful when failure was expected." :
                      "Query failed when success was expected.";

    test->add_result(failed == should_fail, msg);
    test->close_maxscale_connections();

    test->stop_timeout();
}

int main(int argc, char *argv[])
{
    TestConnections *test = new TestConnections(argc, argv);

    test->tprintf("Creating servers");

    for (int i = 0; i < 4; i++)
    {
        test->ssh_maxscale(true, "maxadmin create server server%d %s", i + 1, test->repl->IP[i]);
    }

    /**  Add the servers again */
    add_servers(test);

    test->tprintf("Wait for the monitor to see the new servers");
    sleep(2);

    do_query(test, false);


    test->tprintf("Restarting MaxScale");
    test->restart_maxscale();
    sleep(2);

    do_query(test, false);

    test->check_maxscale_alive();
    test->check_log_err("Fatal", false);
    int rval = test->global_result;
    delete test;
    return rval;
}
