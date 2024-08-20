/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
#include <iostream>
#include <sstream>
#include <string>
#include <maxbase/format.hh>

using std::cerr;
using std::cout;
using std::endl;
using std::flush;
using std::string;
using std::stringstream;

namespace
{

void create_table(TestConnections& test)
{
    MYSQL* pConn = test.maxscale->conn_rwsplit;

    test.try_query(pConn, "DROP TABLE IF EXISTS test.t1");
    test.try_query(pConn, "CREATE TABLE test.t1(id INT)");
}

int i_start = 0;
int n_rows = 20;
int i_end = 0;

void insert_data(TestConnections& test)
{
    MYSQL* pConn = test.maxscale->conn_rwsplit;

    test.try_query(pConn, "BEGIN");

    i_end = i_start + n_rows;

    for (int i = i_start; i < i_end; ++i)
    {
        stringstream ss;
        ss << "INSERT INTO test.t1 VALUES (" << i << ")";
        test.try_query(pConn, "%s", ss.str().c_str());
    }

    test.try_query(pConn, "COMMIT");

    i_start = i_end;
}

void run(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    mxs.wait_for_monitor();

    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    auto normal_status = mxt::ServersInfo::default_repl_states();
    mxs.check_servers_status(normal_status);

    mxs.connect_maxscale();

    test.tprintf("Creating table.");
    create_table(test);

    test.tprintf("Inserting data.");
    insert_data(test);

    test.tprintf("Trying to do manual switchover to server2");
    test.maxctrl("call command mysqlmon switchover MySQL-Monitor server2 server1");

    mxs.wait_for_monitor();
    mxs.check_servers_status({slave, master, slave, slave});

    if (test.ok())
    {
        test.tprintf("Switchover success. Resetting situation using async-switchover.");
        test.maxctrl("call command mariadbmon async-switchover MySQL-Monitor server1");
        // Wait a bit so switch completes, then fetch results.
        mxs.wait_for_monitor(2);
        auto res = test.maxctrl("call command mariadbmon fetch-cmd-result MySQL-Monitor");
        test.expect(res.rc == 0, "fetch-cmd-result failed: %s", res.output.c_str());
        if (test.ok())
        {
            // The output is a json string. Check that it includes "switchover completed successfully".
            auto found = (res.output.find("switchover completed successfully") != string::npos);
            test.expect(found, "Result json did not contain expected message. Result: %s",
                        res.output.c_str());
        }
        mxs.check_servers_status(normal_status);
    }

    if (test.ok())
    {
        test.tprintf("MXS-4605: Monitor should reconnect if command fails due to missing privileges.");
        mxs.stop();
        auto* master_srv = repl.backend(0);
        auto conn = master_srv->open_connection();
        conn->cmd_f("grant slave monitor on *.* to mariadbmon;");
        conn->cmd_f("revoke super, read_only admin on *.* from mariadbmon;");
        repl.sync_slaves();
        // Close connections so monitor does not attempt to kill them.
        conn = nullptr;
        repl.close_connections();
        repl.close_admin_connections();

        mxs.start();

        mxs.check_servers_status(normal_status);
        if (test.ok())
        {
            auto try_switchover = [&](const string& expected_errmsg,
                                      mxt::ServerInfo::bitfield expected_server2_state) {
                const string switch_cmd = "call command mysqlmon switchover MySQL-Monitor server2";
                auto res = test.maxctrl(switch_cmd);
                if (expected_errmsg.empty())
                {
                    if (res.rc == 0)
                    {
                        test.tprintf("Switchover succeeded.");
                    }
                    else
                    {
                        test.add_failure("Switchover failed. Error: %s", res.output.c_str());
                    }
                }
                else
                {
                    if (res.rc == 0)
                    {
                        test.add_failure("Switchover succeeded when it should have failed.");
                    }
                    else
                    {
                        test.tprintf("Switchover failed as expected. Error: %s", res.output.c_str());
                        test.expect(res.output.find(expected_errmsg) != string::npos,
                                    "Did not find expected error message.");
                        mxs.check_print_servers_status({master, expected_server2_state, slave, slave});
                    }
                }
                mxs.wait_for_monitor();
            };

            test.tprintf("Trying switchover, it should fail due to missing privs.");
            try_switchover("Failed to enable read_only on", slave);

            if (test.ok())
            {
                conn = master_srv->open_connection();
                conn->cmd_f("grant super, read_only admin on *.* to mariadbmon;");
                conn = nullptr;

                repl.sync_slaves();
                repl.close_admin_connections();

                test.tprintf("Privileges granted. Switchover should still fail, as monitor connections are "
                             "using the grants of their creation time.");
                // In 23.08 and later, the monitor makes a new connection to master when starting switchover.
                // This connection will immediately have the updated grants. Disabling read-only fails
                // on server2 instead.
                try_switchover("Failed to disable read_only on", mxt::ServerInfo::RUNNING);

                // server2 ends up with replication stopped, not an ideal situation. If auto-rejoin is on,
                // this is not an issue.
                test.tprintf("Rejoining server2");
                mxs.maxctrl("call command mariadbmon rejoin MySQL-Monitor server2");
                mxs.wait_for_monitor(1);
                mxs.check_print_servers_status({master, slave, slave, slave});

                test.tprintf("Switchover should now work.");
                try_switchover("", 0);

                mxs.check_print_servers_status({slave, master, slave, slave});
                mxs.maxctrl("call command mysqlmon switchover MySQL-Monitor");
                mxs.wait_for_monitor();
                mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
            }
        }

        if (!test.ok())
        {
            conn = master_srv->open_connection();
            conn->cmd_f("grant super, read_only admin on *.* to mariadbmon;");
        }
    }

    if (test.ok())
    {
        auto maint = mxt::ServerInfo::MAINT | mxt::ServerInfo::RUNNING;
        test.tprintf("MXS-5075: Switchover but leave old master to maintenance, don't redirect.");
        test.tprintf("First, just test key-value version of switchover.");
        auto res = test.maxctrl("call command mariadbmon switchover monitor=MySQL-Monitor "
                                "new_primary=server2 old_primary=server1 async=0 force=0");
        test.expect(res.rc == 0, "Switchover failed: %s", res.output.c_str());
        mxs.wait_for_monitor();
        mxs.check_print_servers_status({slave, master, slave, slave});

        test.tprintf("Now, switchover without redirecting old master.");
        res = test.maxctrl("call command mariadbmon switchover monitor=MySQL-Monitor "
                           "new_primary=server1 old_primary_maint=1");
        test.expect(res.rc == 0, "Switchover failed: %s", res.output.c_str());
        mxs.wait_for_monitor();
        mxs.check_print_servers_status({master, maint, slave, slave});
        auto servers = mxs.get_servers();
        auto& old_master = servers.get(1);
        test.expect(old_master.slave_connections.empty(),
                    "%s should not have any slave connections but has %zu.",
                    old_master.name.c_str(), old_master.slave_connections.size());

        string clear_cmd = mxb::string_printf("clear server %s maint", old_master.name.c_str());
        mxs.maxctrl(clear_cmd);
        mxs.wait_for_monitor();
        string rejoin_cmd = mxb::string_printf("call command mariadbmon rejoin MySQL-Monitor %s",
                                               old_master.name.c_str());
        res = mxs.maxctrl(rejoin_cmd);
        test.expect(res.rc == 0, "Rejoin failed: %s", res.output.c_str());
        mxs.wait_for_monitor();
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

        test.tprintf("Same, but with auto-rejoin on.");
        mxs.alter_monitor("MySQL-Monitor", "auto_rejoin", "true");
        res = test.maxctrl("call command mariadbmon switchover monitor=MySQL-Monitor old_primary_maint=1");
        test.expect(res.rc == 0, "Switchover failed: %s", res.output.c_str());
        mxs.wait_for_monitor(2);
        mxs.check_print_servers_status({maint, master, slave, slave});
        mxs.maxctrl("clear server server1 maint");
        mxs.wait_for_monitor();
        mxs.check_print_servers_status({slave, master, slave, slave});

        test.maxctrl("call command mariadbmon switchover monitor=MySQL-Monitor old_primary_maint=0");
        mxs.wait_for_monitor(1);
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    }
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, run);
}
