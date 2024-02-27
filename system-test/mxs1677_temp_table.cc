/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-1677: Error messages logged for non-text queries after temporary table is created
 *
 * https://jira.mariadb.org/browse/MXS-1677
 */
#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscale->connect();
    test.try_query(test.maxscale->conn_rwsplit, "CREATE TEMPORARY TABLE test.temp(id INT)");
    test.maxscale->disconnect();

    test.log_excludes("The provided buffer does not contain a COM_QUERY, but a COM_QUIT");
    return test.global_result;
}
