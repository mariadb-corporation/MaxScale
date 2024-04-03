/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file mxs922_restart.cpp MXS-922: Test persisting of configuration changes
 *
 */

#include <maxtest/testconnections.hh>

#define MONITOR_NAME  "mysql-monitor"
#define SERVICE_NAME1 "rwsplit-service"
#define SERVICE_NAME2 "read-connection-router-slave"
#define SERVICE_NAME3 "read-connection-router-master"

void add_servers(TestConnections* test)
{
    test->tprintf("Adding the servers");
    test->reset_timeout();
    test->maxctrl("link monitor " MONITOR_NAME " server1 server2 server3 server4 ");
    test->maxctrl("link service " SERVICE_NAME1 " server1 server2 server3 server4 ");
    test->maxctrl("link service " SERVICE_NAME2 " server1 server2 server3 server4 ");
    test->maxctrl("link service " SERVICE_NAME3 " server1 server2 server3 server4");
}

void do_query(TestConnections* test, bool should_fail)
{
    test->tprintf("Trying to query, expecting %s", should_fail ? "failure" : "success");
    test->reset_timeout();

    test->maxscale->connect_maxscale();

    bool failed = execute_query(test->maxscale->conn_rwsplit, "select @@server_id") == 0;

    const char* msg = should_fail ?
        "Query was successful when failure was expected." :
        "Query failed when success was expected.";

    test->add_result(failed == should_fail, "%s", msg);
    test->maxscale->close_maxscale_connections();
}

int main(int argc, char* argv[])
{
    TestConnections* test = new TestConnections(argc, argv);

    test->tprintf("Creating servers");

    for (int i = 0; i < 4; i++)
    {
        test->maxctrl("create server server" + std::to_string(i + 1) + " " + test->repl->ip4(i));
    }

    /**  Add the servers again */
    add_servers(test);

    test->tprintf("Wait for the monitor to see the new servers");
    test->maxscale->wait_for_monitor();

    do_query(test, false);


    test->tprintf("Restarting MaxScale");
    test->maxscale->restart_maxscale();
    test->maxscale->wait_for_monitor();

    do_query(test, false);

    test->check_maxscale_alive();

    int rval = test->global_result;
    delete test;
    return rval;
}
