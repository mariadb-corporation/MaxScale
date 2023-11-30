/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file bug658.cpp regression case for bug 658 ("readconnroute: client is not closed if backend fails")
 *
 * - Connect all MaxScale
 * - block Mariadb server on Master node by Firewall
 * - execute query
 * - unblock Mariadb server
 * - do same test, but block all backend nodes
 * - check if Maxscale is alive
 */

/*
 *  ilho Raatikka 2014-12-22 22:38:42 UTC
 *  Reproduce:
 *  1. connect readconnroute with mysql client
 *  2. fail the backend server
 *  3. execute query by using mysql client
 *
 *  >> client hangs if write to backend socket doesn't return error (which doesn't happen in many cases)
 *  Comment 1 Markus Mäkelä 2014-12-23 09:19:17 UTC
 *  Added a check for server status before routing the query. Now if the server is down it returns an error.
 */


#include <iostream>
#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->reset_timeout();

    Test->tprintf("Connecting to Maxscale %s", Test->maxscale->ip4());
    Test->maxscale->connect_maxscale();

    printf("Setup firewall to block mysql on master");
    Test->repl->block_node(0);
    Test->maxscale->wait_for_monitor();

    Test->tprintf(
        "Trying query to RWSplit, ReadConn master and ReadConn slave: expecting failure, but not a crash");
    execute_query(Test->maxscale->conn_rwsplit, "show processlist;");
    execute_query(Test->maxscale->conn_master, "show processlist;");
    execute_query(Test->maxscale->conn_slave, "show processlist;");
    Test->maxscale->close_maxscale_connections();

    // Wait three monitor intervals to allow the monitor to detect that the server is up
    Test->repl->unblock_node(0);
    Test->maxscale->wait_for_monitor();

    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
