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
 * Test SEQUENCE related commands
 *
 * This test is only enabled when the backend version is 10.3
 */

#include <maxtest/testconnections.hh>
#include <vector>

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.3");
    TestConnections test(argc, argv);

    test.maxscale->connect();
    test.try_query(test.maxscale->conn_rwsplit, "CREATE SEQUENCE seq");

    std::vector<std::pair<const char*, const char*>> statements =
    {
        {"SELECT NEXT VALUE FOR seq",     "1"},
        {"SELECT PREVIOUS VALUE FOR seq", "1"},
        {"SELECT NEXTVAL(seq)",           "2"},
        {"SELECT LASTVAL(seq)",           "2"},
    };

    for (auto a : statements)
    {
        test.expect(execute_query_check_one(test.maxscale->conn_rwsplit, a.first, a.second) == 0,
                    "Expected '%s' for query: %s",
                    a.second,
                    a.first);
    }

    test.try_query(test.maxscale->conn_rwsplit, "SET SQL_MODE='ORACLE'");

    std::vector<std::pair<const char*, const char*>> oracle_statements =
    {
        {"SELECT seq.nextval", "3"},
        {"SELECT seq.currval", "3"},
    };

    for (auto a : oracle_statements)
    {
        test.expect(execute_query_check_one(test.maxscale->conn_rwsplit, a.first, a.second) == 0,
                    "Expected '%s' for query: %s",
                    a.second,
                    a.first);
    }

    test.try_query(test.maxscale->conn_rwsplit, "DROP SEQUENCE seq");
    test.maxscale->disconnect();

    return test.global_result;
}
