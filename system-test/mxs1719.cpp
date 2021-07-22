/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

void init(TestConnections& test)
{
    MYSQL* pMysql = test.maxscale->conn_rwsplit[0];

    test.try_query(pMysql, "DROP TABLE IF EXISTS MXS_1719");
    test.try_query(pMysql, "CREATE TABLE MXS_1719 (a TEXT, b TEXT)");
    test.try_query(pMysql, "INSERT INTO MXS_1719 VALUES (1, 1)");
}

void run(TestConnections& test)
{
    init(test);

    MYSQL* pMysql = mysql_init(NULL);
    test.expect(pMysql, "Could not create MYSQL handle.");

    const char* zUser = test.maxscale->user_name().c_str();
    const char* zPassword = test.maxscale->password().c_str();
    int port = test.maxscale->rwsplit_port;

    if (mysql_real_connect(pMysql,
                           test.maxscale->ip4(),
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
        test.expect(execute_query_silent(pMysql, q) != 0, "Query '%s' should not succeed", q);

        // Sleep a while, so that the log is flushed.
        sleep(5);
        // This is actually related to MXS-1861 "masking filter logs warnings with
        // multistatements" but it seems excessive to create a specific test for that.
        test.log_excludes("Received data, although expected nothing");

        // This will hang immediately, so we can shorten the timeout.
        test.reset_timeout();
        test.try_query(pMysql, "SELECT * FROM MXS_1719");
    }
    else
    {
        test.expect(false, "Could not connect to MaxScale.");
    }

    mysql_close(pMysql);
}
}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);

    TestConnections test(argc, argv);
    std::string src = mxt::SOURCE_DIR;
    src += "/mxs1719.json";
    std::string dst = std::string(test.maxscale->access_homedir()) + "/mxs1719.json";

    if (test.maxscale->copy_to_node(src.c_str(), dst.c_str()))
    {
        test.maxscale->ssh_node((std::string("chmod a+r ") + dst).c_str(), true);
        test.maxscale->start();
        if (test.ok())
        {
            sleep(10);
            test.maxscale->wait_for_monitor();

            if (test.maxscale->connect_rwsplit() == 0)
            {
                run(test);
            }
            else
            {
                test.expect(false, "Could not connect to RWS.");
            }
        }
    }
    else
    {
        test.expect(false, "Could not copy masking file to MaxScale node.");
    }

    test.maxscale->connect();
    test.try_query(test.maxscale->conn_rwsplit[0], "DROP TABLE MXS_1719");
    test.maxscale->disconnect();

    return test.global_result;
}
