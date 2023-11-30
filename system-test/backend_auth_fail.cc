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
 * @backend_auth_fail.cpp Repeatedly connect to maxscale while the backends reject all connections
 *
 * MaxScale should not crash
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    MYSQL* mysql[1000];
    TestConnections* Test = new TestConnections(argc, argv);

    Test->repl->execute_query_all_nodes((char*) "set global max_connections = 30;");

    for (int x = 0; x < 3; x++)
    {
        Test->tprintf("Creating 100 connections...\n");
        for (int i = 0; i < 100; i++)
        {
            mysql[i] = Test->maxscale->open_readconn_master_connection();
            execute_query_silent(mysql[i], "select 1");
        }

        for (int i = 0; i < 100; i++)
        {
            mysql_close(mysql[i]);
        }
    }

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
