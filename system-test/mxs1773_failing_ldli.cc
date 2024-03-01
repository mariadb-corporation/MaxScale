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
 * MXS-1773: Failing LOAD DATA LOCAL INFILE confuses readwritesplit
 *
 * https://jira.mariadb.org/browse/MXS-1773
 */
#include <maxtest/testconnections.hh>
#include <functional>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscale->connect();
    auto q = std::bind(execute_query, test.maxscale->conn_rwsplit, std::placeholders::_1);
    q("LOAD DATA LOCAL INFILE '/tmp/this-file-does-not-exist.txt' INTO TABLE this_table_does_not_exist");
    q("SELECT 1");
    q("SELECT 2");
    q("SELECT 3");
    test.maxscale->disconnect();

    return test.global_result;
}
