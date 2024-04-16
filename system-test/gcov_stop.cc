/*
 * Copyright (c) 2024 MariaDB plc
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
#include <maxtest/testconnections.hh>
#include <maxbase/string.hh>
#include <sstream>

#include "gcov_common.hh"

void test_main(TestConnections& test)
{
    auto cnf = gcov_config();
    std::string src_dir = mxb::cat(cnf.build_root, "MaxScale");
    std::string build_dir = mxb::cat(cnf.build_root, "build");

    std::ostringstream ss;
    ss << "cd " << build_dir << " &&"
       << " lcov --gcov-tool=$(command -v gcov) -c -d . -o lcov-tested.info &&"
       << " lcov -a lcov-baseline.info -a lcov-tested.info -o lcov.info && "
       << " genhtml --prefix " << src_dir << " -o " << cnf.build_root << "/gcov-report/ lcov.info";

    test.reset_timeout(60 * 30);
    test.maxscale->ssh_node(ss.str(), false);

    // The 000_ prefix makes it sort as the first item in the directory list. Makes it easier to find it.
    // TODO: Move them to a separate directory, currently they're only visible if it's inside LOGS.
    test.maxscale->copy_from_node(cnf.build_root + "/gcov-report/",
                                  mxb::cat(mxt::BUILD_DIR, "/LOGS/000_coverage"));
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
