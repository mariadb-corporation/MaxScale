/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-2631: Dublicate system tables found
 *
 * https://jira.mariadb.org/browse/MXS-2631
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.reset_timeout();

    MYSQL* conn = test.maxscale->open_rwsplit_connection();

    test.add_result(execute_query(conn, "SELECT 1"), "Query should succeed.");

    mysql_close(conn);
    return test.global_result;
}
