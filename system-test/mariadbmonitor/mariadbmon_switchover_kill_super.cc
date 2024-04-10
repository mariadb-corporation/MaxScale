/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

using std::string;

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    mxs.wait_for_monitor();

    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    // Make a direct connection to master, disable auto-reconnect.
    mxt::MariaDB conn(test.logger());
    auto& sett = conn.connection_settings();
    sett.auto_reconnect = false;
    sett.timeout = 3;
    sett.user = "skysql";
    sett.password = "skysql";

    auto srv = test.repl->backend(0);
    conn.open(srv->vm_node().ip4(), srv->port());
    string test_query = "select 123;";
    test.expect(conn.query(test_query) != nullptr, "Query failed.");

    if (test.ok())
    {
        const string switch_cmd = "call command mariadbmon switchover MariaDB-Monitor";
        auto res = mxs.maxctrl(switch_cmd);
        if (res.rc == 0)
        {
            mxs.wait_for_monitor(1);
            mxs.check_print_servers_status({slave, master, slave, slave});
            conn.query(test_query, mxt::MariaDB::Expect::FAIL);
            test.expect(conn.is_open(), "Connection object should exist.");
            test.expect(!conn.ping(), "Ping should fail.");
            if (test.ok())
            {
                test.tprintf("Connection to master was killed during switchover, as it should.");
            }

            res = mxs.maxctrl(switch_cmd);
            if (res.rc == 0)
            {
                mxs.wait_for_monitor(1);
                mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
            }
            else
            {
                test.add_failure("Switchover back failed: %s", res.output.c_str());
            }
        }
        else
        {
            test.add_failure("Switchover failed: %s", res.output.c_str());
        }
    }
}
