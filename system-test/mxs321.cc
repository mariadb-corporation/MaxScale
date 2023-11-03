/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file mx321.cpp regression case for bug MXS-321 ("Incorrect number of connections in list view")
 *
 * - Set max_connections to 100
 * - Create 200 connections
 * - Close connections
 * - Check that list servers shows 0 connections
 */


#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <maxtest/testconnections.hh>

using namespace std;

#define CONNECTIONS 200

void test_main(TestConnections& test)
{
    test.repl->execute_query_all_nodes((char*) "SET GLOBAL max_connections=100");
    std::vector<Connection> conns;

    for (int i = 0; i < CONNECTIONS; i++)
    {
        conns.push_back(test.maxscale->rwsplit());
        conns.push_back(test.maxscale->readconn_master());
        conns.push_back(test.maxscale->readconn_slave());
    }

    for (auto& c : conns)
    {
        c.connect();
    }

    for (auto& c : conns)
    {
        c.disconnect();
    }

    bool ok = false;

    for (int x = 0; x < 10 && !ok; x++)
    {
        ok = true;

        for (int i = 0; i < test.repl->N; i++)
        {
            auto res = test.maxctrl("api get servers/server"
                                    + std::to_string(i + 1)
                                    + " data.attributes.statistics.connections");

            if (res.output != "0")
            {
                ok = false;
            }
        }

        if (!ok)
        {
            sleep(1);
        }
    }

    test.expect(ok, "Expected zero connections to be left on the servers");
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
