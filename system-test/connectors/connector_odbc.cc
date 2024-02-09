/*
 * Copyright (c) 2023 MariaDB plc
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
 * Runs the MariaDB Connector/ODBC test suite against MaxScale
 */
#include "connector_common.hh"
#include <thread>
#include <maxbase/format.hh>

void test_main(TestConnections& test)
{
    // The test takes a while, give it some extra time to complete.
    test.reset_timeout(500);

    if (clone_repo(test, "https://github.com/mariadb-corporation/mariadb-connector-odbc",
                   "master", "mariadb-connector-odbc"))
    {
        auto log_dir = mxb::string_printf("%s/LOGS/%s", mxt::BUILD_DIR, test.shared().test_name.c_str());
        std::string file_name = "connector_odbc_result.txt";
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
           << " && mkdir -p " << log_dir
           << " && echo Test output stored in: " << log_dir << "/" << file_name
           << " && ctest -Q -O " << log_dir << "/" << file_name;

        test.run_shell_command(ss.str(), "Running test suite");
    }
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
