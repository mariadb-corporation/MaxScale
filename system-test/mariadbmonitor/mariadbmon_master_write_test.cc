/*
 * Copyright (c) 2024 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

using std::string;

namespace
{

const string tbl_name = "write_test_table";
const string grants = "SELECT, INSERT, DELETE, CREATE, DROP ON `test`.*";
const string lock_tables = "flush tables with read lock;";
const string unlock_tables = "unlock tables;";

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    auto conn = repl.backend(0)->open_connection();
    conn->cmd_f("drop table if exists test.%s;", tbl_name.c_str());
    conn->try_cmd_f("revoke %s FROM mariadbmon;", grants.c_str());

    mxs.start_and_check_started();
    mxs.sleep_and_wait_for_monitor(1, 1);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    auto find_table = [&repl]() {
        auto conn2 = repl.backend(0)->open_connection();
        bool table_found = false;
        auto res = conn2->query("show tables from test;");
        if (res)
        {
            while (res->next_row())
            {
                if (res->get_string(0) == tbl_name)
                {
                    table_found = true;
                    break;
                }
            }
        }
        return table_found;
    };

    test.tprintf("Write test table should not yet be generated since monitor does not have privileges.");
    sleep(3);
    test.expect(!find_table(), "Table test.%s found when not expected.", tbl_name.c_str());

    test.tprintf("Granting monitor write test table privileges.");
    conn->cmd_f("grant %s TO mariadbmon;", grants.c_str());
    repl.sync_slaves();
    test.tprintf("Restart MaxScale, should create write test table and write rows to it.");
    mxs.restart();
    sleep(3);
    test.expect(find_table(), "Table test.%s not found.", tbl_name.c_str());

    if (test.ok())
    {
        auto write_tests_performed = [&conn]() {
            int rows = -1;
            auto res = conn->query_f("select count(*) from test.%s;", tbl_name.c_str());
            if (res)
            {
                res->next_row();
                rows = res->get_int(0);
            }
            return rows;
        };

        int min_write_tests_expected = 1;
        for (int i = 0; i < 2; i++)
        {
            sleep(2);
            int write_tests = write_tests_performed();
            test.tprintf("Monitor has performed %i write tests.", write_tests);
            test.expect(write_tests >= min_write_tests_expected,
                        "Not enough write tests, expected at least %i.", min_write_tests_expected);
            min_write_tests_expected = write_tests + 1;
        }

        if (test.ok())
        {
            test.tprintf("Block all writes to master, wait a bit and check log for message.");
            int write_tests_before_lock = write_tests_performed();
            test.tprintf("%i write tests before locking database.", write_tests_before_lock);

            conn->cmd(lock_tables);
            test.tprintf("%i write tests right after locking database.", write_tests_performed());

            sleep(5);
            int write_tests_after_lock = write_tests_performed();
            test.tprintf("%i write tests after several seconds.", write_tests_after_lock);
            test.expect(write_tests_after_lock == write_tests_before_lock,
                        "Expected same number of write tests.");
            mxs.log_matches("Primary server server1 failed write test. MariaDB Server storage engine");

            conn->cmd(unlock_tables);
            test.tprintf("Database unlocked.");
            sleep(2);
            int write_tests_after_unlock = write_tests_performed();
            test.tprintf("%i write tests after unlocking tables.", write_tests_after_unlock);
            test.expect(write_tests_after_unlock > write_tests_after_lock,
                        "Expected more than %i write tests.", write_tests_after_lock);
        }

        if (test.ok())
        {
            auto maint = mxt::ServerInfo::MAINT;
            auto running = mxt::ServerInfo::RUNNING;
            auto master = mxt::ServerInfo::master_st;
            auto slave = mxt::ServerInfo::slave_st;

            test.tprintf("Testing failover on write test fail.");
            mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
            mxs.alter_monitor("MariaDB-Monitor", "write_test_fail_action", "failover");

            int write_tests_before_lock = write_tests_performed();
            test.tprintf("%i write tests before locking database.", write_tests_before_lock);
            conn->cmd(lock_tables);
            sleep(1);
            test.tprintf("%i write tests right after locking database.", write_tests_performed());
            sleep(4);
            mxs.wait_for_monitor();
            mxs.check_print_servers_status({maint | running, master, slave, slave});
            conn->cmd(unlock_tables);

            if (test.ok())
            {
                test.tprintf("Failover worker, repeating the test. Locking tables again.");
                conn = repl.backend(1)->open_connection();
                conn->cmd(lock_tables);
                sleep(5);
                mxs.wait_for_monitor();
                mxs.check_print_servers_status({maint | running, maint | running, master, slave});
                conn->cmd(unlock_tables);
                mxs.maxctrl("clear server server2 maint");
                mxs.maxctrl("call command mariadbmon rejoin MariaDB-Monitor server2");
            }

            mxs.maxctrl("clear server server1 maint");
            mxs.maxctrl("call command mariadbmon rejoin MariaDB-Monitor server1");
            mxs.wait_for_monitor();
            mxs.maxctrl("call command mariadbmon switchover MariaDB-Monitor");
            mxs.wait_for_monitor();
            mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
        }
    }

    mxs.alter_monitor("MariaDB-Monitor", "write_test_interval", "0s");
    conn = mxs.open_rwsplit_connection2_nodb();
    conn->cmd_f("drop table if exists test.%s;", tbl_name.c_str());
    conn->cmd_f("revoke %s FROM mariadbmon;", grants.c_str());
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}
