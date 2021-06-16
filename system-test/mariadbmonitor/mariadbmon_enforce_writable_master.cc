#include <maxtest/testconnections.hh>
#include <maxtest/mariadb_connector.hh>

void run_test(TestConnections& test);

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, run_test);
}

void run_test(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    mxs.check_servers_status(mxt::ServersInfo::default_repl_states());

    auto master_conn = test.repl->backend(0)->try_open_connection();
    auto set_ro = [&master_conn]() {
            master_conn->cmd("set global read_only=1;");
        };

    auto check_ro = [&test, &master_conn](bool expected) {
            const char query[] = "select @@read_only;";
            auto res = master_conn->query(query);
            if (res && res->get_col_count() == 1 && res->next_row())
            {
                bool found = res->get_bool(0);
                if (found == expected)
                {
                    test.logger().log_msgf("read_only is %i, as expected.", found);
                }
                else
                {
                    test.add_failure("@@read_only is %i, when %i was expected.", found, expected);
                }
            }
            else
            {
                test.add_failure("Query '%s' failed.", query);
            }
        };

    // Set master read_only. Check that monitor removes it.
    set_ro();
    mxs.wait_for_monitor(2);
    check_ro(false);

    if (test.ok())
    {
        // Try again. This time, stop MaxScale before setting read_only. Monitor should read the journal
        // and see that server1 should be master.
        test.logger().log_msgf("Stop MaxScale, set master read_only, start MaxScale. "
                               "Check monitor removes read_only and detects the master.");
        mxs.stop();
        set_ro();
        check_ro(true);
        mxs.start();
        mxs.wait_for_monitor(2);
        mxs.check_servers_status(mxt::ServersInfo::default_repl_states());
        check_ro(false);
    }
}
