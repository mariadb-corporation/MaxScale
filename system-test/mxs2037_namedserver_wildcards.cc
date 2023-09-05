/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-2037: Wildcards not working with source in NamedServerFilter
 *
 * https://jira.mariadb.org/browse/MXS-2037
 *
 * This test only tests that ip addresses with wildcards are accepted by
 * NamedServerFilter. The actual matching functionality is not tested
 * because the client IPs can change with the different test environments
 * and that would make it complicated to check if the matching is correct.
 */


#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.reset_timeout();
    test.maxscale->connect_maxscale();
    test.add_result(execute_query(test.maxscale->conn_rwsplit, "select 1"), "Can't connect to backend");
    test.maxscale->close_maxscale_connections();
    return test.global_result;
}
