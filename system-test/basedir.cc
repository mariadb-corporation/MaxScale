/*
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
#include <maxbase/format.hh>

using std::string;

// MXS-4881 Test startup options, especially --basedir

namespace
{
void test_maxscale_startup(TestConnections& test, const string& params, bool expect_success);

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    mxs.stop_and_check_stopped();

    test_maxscale_startup(test, "", true);
    test_maxscale_startup(test, "--non-existing-option", false);
    test_maxscale_startup(test, "--basedir=/", true);
    test_maxscale_startup(test, "--basedir=/wrong_dir", false);

    // Even a failed start causes the basedir to be created, delete it.
    mxs.vm_node().run_cmd_output_sudo("rm -rf /wrong_dir");

    if (test.ok())
    {
        // Make a link to root, then use that as basedir.
        string link_name = "/tmp/basedir_link";
        auto link_res = mxs.vm_node().run_cmd_output_sudof("ln -s / %s", link_name.c_str());
        if (link_res.rc == 0)
        {
            test_maxscale_startup(test, "--basedir=/tmp/basedir_link", true);
            mxs.vm_node().run_cmd_output_sudof("rm -rf %s", link_name.c_str());
        }
        else
        {
            test.add_failure("Link creation failed: %s", link_res.output.c_str());
        }
    }
}

void test_maxscale_startup(TestConnections& test, const string& params, bool expect_success)
{
    const int MXS_RUNNING = INT32_MAX;
    auto& mxs_node = test.maxscale->vm_node();
    std::atomic_int mxs_rc {MXS_RUNNING};

    string mxs_cmd = mxb::string_printf("maxscale -d --user=root %s", params.c_str());

    auto thread_func = [&mxs_node, &mxs_cmd, &mxs_rc]() {
        auto res = mxs_node.run_cmd_output_sudo(mxs_cmd);
        mxs_rc = res.rc;
    };
    test.tprintf("Trying to start MaxScale with '%s'.", mxs_cmd.c_str());
    std::thread mxs_thread(thread_func);

    sleep(2);

    auto pidof_res = mxs_node.run_cmd_output("pidof maxscale");
    if (pidof_res.rc == 0)
    {
        if (pidof_res.output.empty())
        {
            test.add_failure("pidof succeeded, yet returned empty.");
            exit(EXIT_FAILURE);
        }
        else
        {
            test.tprintf("Killing process %s", pidof_res.output.c_str());
            auto kill_res = mxs_node.run_cmd_output_sudof("kill %s", pidof_res.output.c_str());
            if (kill_res.rc == 0)
            {
                mxs_thread.join();
                test.expect(mxs_rc != MXS_RUNNING, "MaxScale running even after kill.");
                test.expect(expect_success, "MaxScale started successfully when failure was expected.");
            }
            else
            {
                test.add_failure("Kill failed. Error %i: %s", kill_res.rc, kill_res.output.c_str());
                exit(EXIT_FAILURE);
            }
        }
    }
    else
    {
        // This typically means MaxScale already exited.
        if (mxs_rc != MXS_RUNNING)
        {
            // MaxScale already exited, startup must have failed. MaxScale can still return 0.
            mxs_thread.join();
            test.expect(!expect_success, "MaxScale startup failed when success was expected.");
        }
        else
        {
            test.add_failure("pidof failed, yet MaxScale is still running.");
            exit(EXIT_FAILURE);
        }
    }
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
