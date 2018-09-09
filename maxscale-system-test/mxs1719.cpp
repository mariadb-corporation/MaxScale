/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include "testconnections.h"

using namespace std;

namespace
{

void init(TestConnections& test)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    test.try_query(pMysql, "DROP TABLE IF EXISTS MXS_1719");
    test.try_query(pMysql, "CREATE TABLE MXS_1719 (a TEXT, b TEXT)");
    test.try_query(pMysql, "INSERT INTO MXS_1719 VALUES (1, 1)");
}

void run(TestConnections& test)
{
    init(test);

    MYSQL* pMysql = mysql_init(NULL);
    test.assert(pMysql, "Could not create MYSQL handle.");

    const char* zUser = test.maxscales->user_name;
    const char* zPassword = test.maxscales->password;
    int port = test.maxscales->rwsplit_port[0];

    if (mysql_real_connect(pMysql,
                           test.maxscales->IP[0],
                           zUser,
                           zPassword,
                           "test",
                           port,
                           NULL,
                           CLIENT_MULTI_STATEMENTS))
    {
        const char* q = "UPDATE MXS_1719 SET a=1; UPDATE MXS_1719 SET a=1;";
        // One multi-statement with two UPDATEs. Note: This query should fail
        // with 2.3 now that function blocking has been added
        test.assert(execute_query_silent(pMysql, q) != 0, "Query '%s' should not succeed", q);

        // Sleep a while, so that the log is flushed.
        sleep(5);
        // This is actually related to MXS-1861 "masking filter logs warnings with
        // multistatements" but it seems excessive to create a specific test for that.
        test.log_excludes(0, "Received data, although expected nothing");

        // This will hang immediately, so we can shorten the timeout.
        test.set_timeout(5);
        test.try_query(pMysql, "SELECT * FROM MXS_1719");
        test.stop_timeout();
    }
    else
    {
        test.assert(false, "Could not connect to MaxScale.");
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);

    TestConnections test(argc, argv);
    std::string src = test_dir;
    src += "/mxs1719.json";

    if (test.maxscales->copy_to_node(0, src.c_str(), "/home/vagrant/mxs1719.json") == 0)
    {
        if (test.maxscales->start() == 0)
        {
            sleep(10);
            test.maxscales->wait_for_monitor();

            if (test.maxscales->connect_rwsplit() == 0)
            {
                run(test);
            }
            else
            {
                test.assert(false, "Could not connect to RWS.");
            }
        }
        else
        {
            test.assert(false, "Could not start MaxScale.");
        }
    }
    else
    {
        test.assert(false, "Could not copy masking file to MaxScale node.");
    }

    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE MXS_1719");
    test.maxscales->disconnect();

    return test.global_result;
}
