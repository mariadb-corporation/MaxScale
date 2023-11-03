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
 * MXS-1889: A single remaining master is valid for readconnroute configured with 'router_options=slave'
 *
 * https://jira.mariadb.org/browse/MXS-1889
 */

#include <maxtest/testconnections.hh>
#include <iostream>
#include <string>

using namespace std;

namespace
{

string get_server_id(TestConnections& test, MYSQL* pMysql)
{
    string id;

    int rv = mysql_query(pMysql, "SELECT @@server_id");
    test.add_result(rv, "Could not execute query.");

    if (rv == 0)
    {
        MYSQL_RES* pResult = mysql_store_result(pMysql);
        test.expect(pResult, "Could not store result.");

        if (pResult)
        {
            unsigned int n = mysql_field_count(pMysql);
            test.expect(n == 1, "Unexpected number of fields.");

            MYSQL_ROW pzRow = mysql_fetch_row(pResult);
            test.expect(pzRow, "Returned row was NULL.");

            if (pzRow)
            {
                id = pzRow[0];
            }

            mysql_free_result(pResult);
        }
    }

    return id;
}
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.tprintf("Taking down all slaves.");
    test.repl->stop_node(1);
    test.repl->stop_node(2);
    test.repl->stop_node(3);

    test.tprintf("Giving monitor time to detect the situation...");
    test.maxscale->wait_for_monitor();

    test.maxscale->connect();

    // All slaves down, so we expect a connection to the master.
    string master_id = get_server_id(test, test.maxscale->conn_slave);
    test.tprintf("Master id: %s", master_id.c_str());

    test.maxscale->disconnect();

    test.tprintf("Starting all slaves.");
    test.repl->start_node(3);
    test.repl->start_node(2);
    test.repl->start_node(1);

    test.tprintf("Giving monitor time to detect the situation...");
    test.maxscale->wait_for_monitor();

    test.maxscale->connect();

    string slave_id = get_server_id(test, test.maxscale->conn_slave);
    test.tprintf("Server id: %s", slave_id.c_str());
    test.expect(slave_id != master_id, "Expected something else but %s", master_id.c_str());

    return test.global_result;
}
