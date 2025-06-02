/*
 * Copyright (c) 2025 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
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

    if (clone_repo(test, "https://github.com/mariadb/mariadb-connector-c",
                   "3.4", "mariadb-connector-c"))
    {
        auto user = create_user(test);
        auto log_dir = mxb::string_printf("%s/LOGS/%s", mxt::BUILD_DIR, test.shared().test_name.c_str());
        std::string file_name = "connector_c_result.txt";
        std::ostringstream ss;
        ss << "cd mariadb-connector-c "
           << " && export MARIADB_CC_TEST=1"
           << " && export MYSQL_TEST_HOST=" << test.maxscale->ip()
           << " && export MYSQL_TEST_USER=connector"
           << " && export MYSQL_TEST_PASSWD=connector"
            // The 3.4 branch expects TLS on all ports so set both to 4007 where TLS is enabled.
           << " && export MYSQL_TEST_PORT=4007"
           << " && export MYSQL_TEST_SSL_PORT=4007"
           << " && export MYSQL_TEST_TLS=0"
           << " && export MARIADB_TLS_DUMMY_PORT=4999"
           << " && export MYSQL_TEST_SCHEMA=test"
           << " && export srv=maxscale"
           << " && cmake -DWITH_UNIT_TESTS=Y ."
           << " && make -j " << std::thread::hardware_concurrency()
           << " && cd unittest/libmariadb"
           << " && mkdir -p " << log_dir
           << " && echo Test output stored in: " << log_dir << "/" << file_name
            // TODO: The 'tls' test fails due to various certificate validation issues
           << " && ctest -E tls --output-on-failure -Q -O " << log_dir << "/" << file_name;

        test.run_shell_command(ss.str(), "Running test suite");
    }
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
