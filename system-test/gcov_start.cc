/*
 * Copyright (c) 2024 MariaDB plc
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
#include <maxtest/testconnections.hh>
#include <maxbase/string.hh>

#include "gcov_common.hh"

void test_main(TestConnections& test)
{
    auto cnf = gcov_config();
    std::string src_dir = mxb::cat(cnf.build_root, "/MaxScale");
    std::string build_dir = mxb::cat(cnf.build_root, "/build");

    auto old_verbose = test.verbose();
    test.set_verbose(true);

    auto cmd = [&](auto ... args){
        test.reset_timeout(60 * 60);
        std::string arg_str = mxb::cat(args ...);
        test.tprintf("%s", arg_str.c_str());
        test.maxscale->ssh_node(arg_str, false);
    };

    if (cnf.build)
    {
        // The "universal" git installer
        cmd("(sudo apt update && sudo apt -y install git) || sudo dnf -y install git");

        cmd("sudo mkdir -p ", cnf.build_root, " ", build_dir);
        cmd("sudo chmod -R a+rw ", cnf.build_root);
        cmd("git clone --depth=1 --branch=", cnf.branch, " ", cnf.repo, " ", src_dir);
        cmd(src_dir, "/BUILD/install_build_deps.sh");
        cmd("cd ", build_dir,
            " && cmake ", src_dir, " ", cnf.cmake_flags,
            " && make -j $(grep -c 'processor' /proc/cpuinfo)",
            " && ctest -j 100 --output-on-failure",
            " && sudo make install");
        cmd("sudo ", build_dir, "/postinst");

        // The build directory must be writable by the maxscale user
        cmd("sudo chmod -R a+rw ", cnf.build_root);
    }

    // Create an empty baseline coverage file. This will then be combined with the actual coverage info.
    cmd("cd ", build_dir, " && lcov --gcov-tool=$(command -v gcov) -c -i -d . -o lcov-baseline.info");

    test.set_verbose(old_verbose);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
