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
 * @file mx314.cpp regression case for bug MXS-314 ("Read Write Split Error with Galera Nodes")
 * - try prepared stmt 'SELECT 1,1,1,1...." with different number of '1'
 * - check if Maxscale alive
 */

#include <maxtest/testconnections.hh>
#include <sstream>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    std::ostringstream ss;
    ss << "SELECT 1";

    test.maxscale->connect();

    MYSQL_STMT* stmt = mysql_stmt_init(test.maxscale->conn_rwsplit);

    for (int i = 0; i < 50; i++)
    {
        auto query = ss.str();
        test.reset_timeout();
        test.add_result(mysql_stmt_prepare(stmt, query.c_str(), query.length()),
                        "Failed at %d: %s\n", i,
                        mysql_error(test.maxscale->conn_rwsplit));
        test.add_result(mysql_stmt_reset(stmt), "Failed at %d: %s\n", i,
                        mysql_error(test.maxscale->conn_rwsplit));

        for (int x = 0; x < 17; x++)
        {
            ss << "," << i;
        }
    }

    test.reset_timeout();
    mysql_stmt_close(stmt);
    test.maxscale->disconnect();

    return test.global_result;
}
