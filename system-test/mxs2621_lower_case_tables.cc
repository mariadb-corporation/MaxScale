/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-2621: Incorrect SQL if lower_case_table_names is used.
 * https://jira.mariadb.org/browse/MXS-2621
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.maxscale->connect();
    test.try_query(test.maxscale->conn_rwsplit, "SELECT 123");
    test.maxscale->disconnect();
    return test.global_result;
}
