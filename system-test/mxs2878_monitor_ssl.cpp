/**
 * Covers the following bugs:
 * MXS-2878: Monitor connections do not insist on SSL being used
 * MXS-2896: Server wrongly in Running state after failure to connect
 */

#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

using std::string;

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    mxs.check_servers_status(mxt::ServersInfo::default_repl_states());
    mxs.stop();

    if (test.ok())
    {
        // Disable ssl on backends by moving ssl config files.
        auto move_file = [&](int i, const string& source, const string& dest) {
                auto cmd = mxb::string_printf("mv %s %s", source.c_str(), dest.c_str());
                auto res = repl.ssh_output(cmd, i, true);

                if (res.rc != 0)
                {
                    test.add_failure("Failed to move ssl-config. '%s' returned %i: %s",
                                     cmd.c_str(), res.rc, res.output.c_str());
                }
            };

        const string orig_file = "/etc/my.cnf.d/ssl.cnf";
        const string temp_file = "/tmp/ssl.cnf";

        test.logger().log_msgf("Disabling ssl on backends by moving '%s' to '%s'.",
                               orig_file.c_str(), temp_file.c_str());
        for (int i = 0; i < repl.N; i++)
        {
            repl.stop_node(i);
            repl.ssh_output("rm -f " + temp_file, i, true);
            move_file(i, orig_file, temp_file);
            repl.start_node(i);
        }

        if (test.ok())
        {
            mxs.start();
            mxs.wait_for_monitor();
            auto down = mxt::ServerInfo::DOWN;
            auto status = mxs.get_servers();
            status.print();
            status.check_servers_status({down, down, down, down});
            mxs.stop();
        }

        // Fix situation by moving files back.
        test.logger().log_msgf("Restoring ssl on backends by moving '%s' to '%s'.",
                               temp_file.c_str(), orig_file.c_str());
        for (int i = 0; i < repl.N; i++)
        {
            repl.stop_node(i);
            move_file(i, temp_file, orig_file);
            repl.start_node(i);
        }

        mxs.start();
        mxs.wait_for_monitor();
        auto status = mxs.get_servers();
        status.print();
        status.check_servers_status(mxt::ServersInfo::default_repl_states());
    }
}
