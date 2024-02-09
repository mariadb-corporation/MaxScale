/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file rwsplit_connect.cpp Check that there is one connection to Master and one connection to one of slaves
 * - connecto to RWSplit
 * - check number of connections on every backend, expect one active Slave and one connection to Master
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->reset_timeout();
    Test->repl->connect();

    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscale->ip());
    Test->maxscale->connect_rwsplit();

    unsigned int conn_num;
    unsigned int all_conn = 0;
    Test->tprintf("Sleeping 5 seconds\n");
    sleep(5);
    Test->tprintf("Checking number of connections ot backend servers\n");
    for (int i = 0; i < Test->repl->N; i++)
    {
        conn_num =
            get_conn_num(Test->repl->nodes[i],
                         Test->maxscale->ip(),
                         Test->maxscale->hostname(),
                         (char*) "test");
        Test->tprintf("connections: %u\n", conn_num);
        if ((i == 0) && (conn_num != 1))
        {
            Test->add_result(1,
                             " Master should have only 1 connection, but it has %d connection(s)\n",
                             conn_num);
        }
        all_conn += conn_num;
    }
    if (all_conn != 2)
    {
        Test->add_result(1,
                         "there should be two connections in total: one to master and one to one of slaves, but number of connections is %d\n",
                         all_conn);
    }

    Test->maxscale->close_rwsplit();
    Test->repl->close_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
