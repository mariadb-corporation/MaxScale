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

    test.try_query(pMysql, "DROP TABLE IF EXISTS masking_auto_firewall");
    test.try_query(pMysql, "CREATE TABLE masking_auto_firewall (a TEXT, b TEXT)");
    test.try_query(pMysql, "INSERT INTO masking_auto_firewall VALUES ('hello', 'world')");
}

void run(TestConnections& test)
{
    init(test);

    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    int rv;

    // This should go through, a is simply masked.
    static const char* zMasked_query = "SELECT a, b FROM masking_auto_firewall";
    test.tprintf("Executing '%s', SHOULD succeed.", zMasked_query);
    rv = execute_query(pMysql, "%s", zMasked_query);
    test.add_result(rv, "Could NOT execute query '%s'.", zMasked_query);

    // This should NOT go through as a function is used with a masked column.
    static const char* zRejected_query = "SELECT LENGTH(a), b FROM masking_auto_firewall";
    test.tprintf("Executing '%s', should NOT succeed.", zRejected_query);
    rv = execute_query_silent(pMysql , zRejected_query);
    test.add_result(rv == 0, "COULD execute query '%s'.", zRejected_query);
}

}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);

    TestConnections test(argc, argv);

    std::string json_file("/masking_auto_firewall.json");
    std::string from = test_dir + json_file;
    std::string to = "/home/vagrant" + json_file;

    if (test.maxscales->copy_to_node(0, from.c_str(), to.c_str()) == 0)
    {
        if (test.maxscales->start() == 0)
        {
            sleep(2);
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

    return test.global_result;
}
