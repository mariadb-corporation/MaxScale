/*
 * Copyright (c) 2023 MariaDB plc
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

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    auto* srv1 = repl.backend(0);

    mxs.wait_for_monitor();

    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    std::vector<mxt::MariaDB> killable_conns;
    string test_query = "select 123;";

    auto prepare_admin_conn = [&](std::string_view user, std::string_view pw) {
        // Make a direct connection to master, disable auto-reconnect.
        mxt::MariaDB conn(test.logger());
        auto& sett = conn.connection_settings();
        sett.auto_reconnect = false;
        sett.timeout = 3;
        sett.user = user;
        sett.password = pw;
        conn.open(srv1->vm_node().ip4(), srv1->port());
        test.expect(conn.query(test_query) != nullptr, "Query failed.");
        return conn;
    };

    killable_conns.emplace_back(prepare_admin_conn("skysql", "skysql"));
    killable_conns.emplace_back(prepare_admin_conn("repl", "repl"));
    killable_conns.emplace_back(prepare_admin_conn("maxuser", "maxuser"));

    auto rws_conn = mxs.open_rwsplit_connection2_nodb();
    const char ro_admin_un[] = "ro_admin";
    auto ro_admin_user = rws_conn->create_user(ro_admin_un, "%", ro_admin_un);
    ro_admin_user.grant("READ_ONLY ADMIN ON *.*");
    killable_conns.emplace_back(prepare_admin_conn(ro_admin_un, ro_admin_un));

    const char super_un[] = "super";
    auto super_user = rws_conn->create_user(super_un, "%", super_un);
    super_user.grant("SUPER ON *.*");
    auto super_conn = prepare_admin_conn(super_un, super_un);

    // In version 10.11 and later, SUPER no longer includes READ_ONLY ADMIN, so it should not be kicked out.
    // In versions earlier than that, it should be kicked out.
    const bool should_kill_super = srv1->version().as_number() < 101100;
    if (should_kill_super)
    {
        killable_conns.emplace_back(std::move(super_conn));
    }

    if (test.ok())
    {
        const string switch_cmd = "call command mariadbmon switchover MariaDB-Monitor";
        auto res = mxs.maxctrl(switch_cmd);
        if (res.rc == 0)
        {
            mxs.wait_for_monitor(1);
            mxs.check_print_servers_status({slave, master, slave, slave});

            for (auto& conn : killable_conns)
            {
                test.expect(conn.is_open(), "Connection should be open.");
                test.expect(!conn.ping(), "Ping should fail.");
                conn.query(test_query, mxt::MariaDB::Expect::FAIL);
                if (test.ok())
                {
                    test.tprintf("User '%s' connection to primary server was killed during switchover, "
                                 "as it should.", conn.connection_settings().user.c_str());
                }
            }

            if (!should_kill_super)
            {
                test.expect(super_conn.is_open(), "Connection should be open.");
                test.expect(super_conn.ping(), "Ping should succeed.");
                super_conn.query(test_query);

                if (test.ok())
                {
                    test.tprintf("User '%s' connection to primary server was preserved during switchover, "
                                 "as it should.", super_conn.connection_settings().user.c_str());
                }
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
