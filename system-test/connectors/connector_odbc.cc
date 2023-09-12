/*
 * Copyright (c) 2023 MariaDB plc
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
 * Runs the MariaDB Connector/ODBC test suite against MaxScale
 */
#include "connector_common.hh"
#include <thread>

void test_main(TestConnections& test)
{
    // The test takes a while, give it some extra time to complete.
    test.reset_timeout(500);

    if (clone_repo(test, "https://github.com/mariadb-corporation/mariadb-connector-odbc",
                   "master", "mariadb-connector-odbc"))
    {
        std::ostringstream ss;
        ss << "cd mariadb-connector-odbc "
           << " && export TEST_DSN=maodbc_test"
           << " && export TEST_DRIVER=maodbc_test"
           << " && export TEST_SERVER=" << test.maxscale->ip()
           << " && export TEST_UID=" << test.maxscale->user_name()
           << " && export TEST_PASSWORD=" << test.maxscale->password()
           << " && export TEST_PORT=4006"
           << " && export TEST_SCHEMA=test"
           << " && export srv=maxscale"
           << " && cmake -DWITH_UNIT_TESTS=Y ."
           << " && make -j " << std::thread::hardware_concurrency()
           << " && cd test"
           << " && export ODBCINI=$PWD/odbc.ini"
           << " && export ODBCSYSINI=$PWD"
           << " && ctest --output-on-failure";

        test.run_shell_command(ss.str(), "Running test suite");
    }
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
