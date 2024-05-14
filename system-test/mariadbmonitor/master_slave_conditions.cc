/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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
#include <maxtest/mariadb_connector.hh>

using std::string;
using std::vector;
using mxt::ServersInfo;
using mxt::ServerInfo;

namespace
{
const char set_readonly_query[] = "SET GLOBAL read_only='%s'";
const char stop[] = "stop slave;";
const char start[] = "start slave;";

void test_main(TestConnections& test)
{
    auto master_st = ServerInfo::MASTER | ServerInfo::RUNNING;
    auto slave_st = ServerInfo::SLAVE | ServerInfo::RUNNING;
    auto running_st = ServerInfo::RUNNING;
    auto down_st = ServerInfo::DOWN;

    const string mon_name = "MariaDB-Monitor";
    const string master_cond = "master_conditions";
    const string slave_cond = "slave_conditions";

    auto master_3slaves = {master_st, slave_st, slave_st, slave_st};
    auto master_3running = {master_st, running_st, running_st, running_st};
    auto slave_3running = {slave_st, running_st, running_st, running_st};
    auto down_3running = {down_st, running_st, running_st, running_st};
    auto down_3slaves = {down_st, slave_st, slave_st, slave_st};
    auto all_running = {running_st, running_st, running_st, running_st};

    auto& mxs = *test.maxscale;

    auto reset = [&]() {
            test.repl->stop_slaves();
            mxs.alter_monitor(mon_name, master_cond, "none");
            test.repl->connect();
            test.repl->replicate_from(1, 0);
            test.repl->replicate_from(2, 0);
            test.repl->replicate_from(3, 0);
        };

    auto alter_monitor = [&](const string& setting, const string& value) {
            test.tprintf("Set %s=%s.", setting.c_str(), value.c_str());
            mxs.alter_monitor(mon_name, setting, value);
            mxs.wait_for_monitor();
        };

    auto check_io_connecting = [&test](const ServerInfo& srv_info) {
            if (srv_info.slave_connections.empty())
            {
                test.add_failure("'%s' has no slave connections", srv_info.name.c_str());
            }
            else
            {
                bool ok = srv_info.slave_connections[0].io_running
                    == ServerInfo::SlaveConnection::IO_State::CONNECTING;
                test.expect(ok, "Slave_IO_Running of '%s' is not 'Connecting'", srv_info.name.c_str());
            }
        };

    auto set_wrong_repl_pw = [&test](int slave, int master) {
        bool rval = false;
        auto be_slave = test.repl->backend(slave);
        if (be_slave->ping_or_open_admin_connection())
        {
            auto conn = be_slave->admin_connection();
            conn->cmd("STOP SLAVE;");
            string change_master = "CHANGE MASTER TO MASTER_PASSWORD = 'repll'";
            conn->cmd(change_master);
            if (conn->cmd_f("START SLAVE;"))
            {
                rval = true;
            }
        }
        else
        {
            test.add_failure("Connection to slave failed.");
        }
        return rval;
    };

    mxs.check_servers_status(master_3slaves);
    const int master_ind = 0;
    if (test.ok())
    {
        test.tprintf("Stop all slaves, first server should remain [Master].");
        test.repl->stop_slaves();
        mxs.wait_for_monitor();
        mxs.check_servers_status(master_3running);

        alter_monitor(master_cond, "connected_slave,running_slave");
        test.tprintf("Should lose [Master], but gain [Slave].");
        mxs.wait_for_monitor();
        mxs.check_servers_status(slave_3running);

        test.tprintf("Have one slave start replicating. Should regain [Master].");
        const int slave_ind = 1;
        test.repl->connect(slave_ind);
        test.repl->replicate_from(slave_ind, master_ind);
        mxs.wait_for_monitor();
        mxs.check_servers_status({master_st, slave_st});

        test.tprintf("Shut down the slave. Should lose [Master].");
        test.repl->stop_node(slave_ind);
        mxs.wait_for_monitor();
        mxs.check_servers_status({slave_st, down_st});
        test.repl->start_node(slave_ind);

        alter_monitor(master_cond, "connected_slave");
        test.tprintf("Stopping replication should lose [Master].");
        test.repl->connect(slave_ind);
        test.try_query(test.repl->nodes[slave_ind], "STOP SLAVE;");
        mxs.wait_for_monitor();
        mxs.check_servers_status(slave_3running);

        test.tprintf("Restart replication, then shut down slave. Should keep [Master].");
        test.try_query(test.repl->nodes[slave_ind], "START SLAVE;");
        mxs.wait_for_monitor();
        test.repl->stop_node(slave_ind);
        mxs.wait_for_monitor();
        mxs.check_servers_status({master_st, down_st});
        test.repl->start_node(slave_ind);

        test.tprintf("Stop master, then start it. If slave does not reconnect quickly, "
                     "should not get [Master]");
        test.repl->stop_node(master_ind);
        ::sleep(2);
        test.repl->start_node(master_ind);
        mxs.wait_for_monitor(2);
        auto status = mxs.get_servers();
        check_io_connecting(status.get(slave_ind));
        status.check_servers_status({slave_st, slave_st});

        alter_monitor(master_cond, "connecting_slave");
        test.tprintf("Should get [Master] even when slave is not yet connected.");
        mxs.wait_for_monitor(2);
        status = mxs.get_servers();
        check_io_connecting(status.get(slave_ind));
        status.check_servers_status({master_st, slave_st});
        mxs.check_servers_status({master_st, slave_st});

        test.tprintf("Try replication with wrong credentials. Should not get [Master].");
        if (set_wrong_repl_pw(slave_ind, master_ind))
        {
            mxs.wait_for_monitor(2);
            status = mxs.get_servers();
            check_io_connecting(status.get(slave_ind));
            status.check_servers_status({slave_st, running_st});
        }

        reset();
    }

    if (test.ok())
    {
        auto relay_st = ServerInfo::RELAY | ServerInfo::SLAVE | ServerInfo::RUNNING;
        auto master_slave_chain = {master_st, relay_st, relay_st, slave_st};

        auto start_node_refresh_slave = [&test](int node_ind) {
                test.repl->start_node(node_ind);
                // Ensure slave reconnects to the server which just started.
                test.repl->connect(node_ind + 1);
                test.repl->replicate_from(node_ind + 1, node_ind);
            };

        test.tprintf("Arrange a chained topology: M->S1->S2->S3");
        test.repl->connect();
        test.repl->replicate_from(2, 1);
        test.repl->replicate_from(3, 2);
        mxs.wait_for_monitor();
        mxs.check_servers_status(master_slave_chain);

        test.tprintf("Stop a slave connection, should lose [Slave]");
        const int slave_ind = 1;
        test.try_query(test.repl->nodes[slave_ind], "STOP SLAVE;");
        mxs.wait_for_monitor();
        mxs.check_servers_status(master_3running);

        test.tprintf("Start the slave connection with wrong pw, should not get [Slave]");
        if (set_wrong_repl_pw(slave_ind, master_ind))
        {
            mxs.wait_for_monitor(2);
            auto status = mxs.get_servers();
            check_io_connecting(status.get(slave_ind));
            status.check_servers_status({master_st, running_st, running_st, running_st});
        }

        test.tprintf("Restore correct credentials, should regain [Slave]");
        test.try_query(test.repl->nodes[slave_ind], "STOP SLAVE;");
        test.try_query(test.repl->nodes[slave_ind], "CHANGE MASTER TO MASTER_PASSWORD = 'repl'");
        test.try_query(test.repl->nodes[slave_ind], "START SLAVE;");
        mxs.wait_for_monitor(2);
        mxs.check_servers_status(master_slave_chain);

        test.tprintf("Shut down a relay, should keep [Slave]");
        test.repl->stop_node(slave_ind);
        mxs.wait_for_monitor();
        mxs.check_servers_status({master_st, down_st, slave_st, slave_st});     // also loses relay
        start_node_refresh_slave(slave_ind);
        mxs.wait_for_monitor();

        test.tprintf("Shut down master, should keep [Slave]");
        test.repl->stop_node(master_ind);
        mxs.wait_for_monitor();
        mxs.check_servers_status(down_3slaves);
        start_node_refresh_slave(master_ind);
        mxs.wait_for_monitor();

        alter_monitor(slave_cond, "linked_master");
        test.tprintf("Replication chain should still be valid");
        mxs.wait_for_monitor();
        mxs.check_servers_status(master_slave_chain);

        test.tprintf("Shut down a relay, should lose [Slave]s");
        test.repl->stop_node(slave_ind);
        mxs.wait_for_monitor();
        mxs.check_servers_status({master_st, down_st, running_st, running_st});
        start_node_refresh_slave(slave_ind);
        mxs.wait_for_monitor();

        test.tprintf("Stop master, then start it. If slave does not reconnect quickly, "
                     "should not get any [Slave]s");
        test.repl->stop_node(master_ind);
        ::sleep(2);
        test.repl->start_node(master_ind);
        mxs.wait_for_monitor(2);
        auto status = mxs.get_servers();
        check_io_connecting(status.get(slave_ind));
        status.check_servers_status(master_3running);
        // Ensure slave reconnects to master.
        test.repl->connect(slave_ind);
        test.repl->replicate_from(slave_ind, master_ind);

        alter_monitor(slave_cond, "running_master");
        test.tprintf("Replication chain should still be valid");
        mxs.check_servers_status(master_slave_chain);

        test.tprintf("Shut down master, should lose [Slave]");
        test.repl->stop_node(master_ind);
        mxs.wait_for_monitor();
        mxs.check_servers_status(down_3running);
        start_node_refresh_slave(master_ind);

        alter_monitor(slave_cond, "writable_master");
        test.tprintf("Set master to read_only, should lose [Slave]");
        test.repl->connect(master_ind);
        test.try_query(test.repl->nodes[master_ind], set_readonly_query, "ON");
        mxs.wait_for_monitor();
        mxs.check_servers_status(all_running);
        test.try_query(test.repl->nodes[master_ind], set_readonly_query, "OFF");
    }

    reset();

    if (test.ok())
    {
        // MXS-5031: Master should never be set to read_only by enforce_read_only_slaves=1.
        alter_monitor(master_cond, "none");
        alter_monitor(slave_cond, "none");
        mxs.wait_for_monitor();
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

        auto expect_read_only = [&test](int node, bool expected) {
            string res = test.repl->backend(node)->admin_connection()->simple_query(
                "select @@global.read_only;");
            if ((res == "1" && expected) || (res == "0" && !expected))
            {
                // correct
            }
            else
            {
                test.add_failure("Expected read_only of node %i to be %i, found '%s'.",
                                 node, expected, res.c_str());
            }
        };

        auto& repl = *test.repl;
        for (int i = 0; i < repl.N; i++)
        {
            expect_read_only(i, false);
        }

        if (test.ok())
        {
            test.tprintf("Enabling enforce_read_only_slaves. Check that read_only is set on slaves but not "
                         "on master.");
            alter_monitor("enforce_read_only_slaves", "true");
            mxs.wait_for_monitor();
            expect_read_only(0, false);
            for (int i = 1; i < repl.N; i++)
            {
                expect_read_only(i, true);
            }

            test.tprintf("Modify master_conditions and cluster so that master gets [Slave]. Check that "
                         "master is not set to read_only.");
            alter_monitor(master_cond, "connected_slave");
            for (int i = 1; i < repl.N; i++)
            {
                repl.backend(i)->admin_connection()->cmd("stop slave;");
            }
            mxs.wait_for_monitor();
            mxs.check_print_servers_status({slave_st, running_st, running_st, running_st});
            expect_read_only(0, false);

            test.tprintf("Turn off enforce_read_only_slaves, enable writes, restart replication.");
            alter_monitor("enforce_read_only_slaves", "false");

            for (int i = 1; i < repl.N; i++)
            {
                auto conn = repl.backend(i)->admin_connection();
                conn->cmd("set global read_only = 0;");
                conn->cmd("start slave;");
            }
            mxs.sleep_and_wait_for_monitor(1, 2);

            mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
            for (int i = 0; i < repl.N; i++)
            {
                expect_read_only(i, false);
            }

            // MXS-5067 enforce_read_only_servers
            const char enforce_servers[] = "enforce_read_only_servers";
            test.tprintf("Set server2 to maintenance, server3 to draining and shutdown server4. None"
                         "should have read_only.");
            alter_monitor(master_cond, "connected_slave,running_slave");
            mxs.maxctrl("set server server2 maint");
            mxs.maxctrl("set server server3 drain");
            int stopped_node = 3;
            repl.stop_node(stopped_node);

            mxs.wait_for_monitor(2);
            for (int i = 0; i < stopped_node; i++)
            {
                expect_read_only(i, false);
            }

            test.tprintf("Enable %s, check that read_only is set on server3.", enforce_servers);
            alter_monitor(enforce_servers, "true");
            mxs.wait_for_monitor();
            auto maint_st = mxt::ServerInfo::MAINT | mxt::ServerInfo::RUNNING;
            mxs.check_servers_status({master_st, maint_st, mxt::ServerInfo::DRAINED | slave_st, down_st});
            expect_read_only(0, false);
            expect_read_only(1, false);
            expect_read_only(2, true);

            test.tprintf("Stop replication on server2 & 3. Read-only should not change.");
            repl.backend(1)->admin_connection()->cmd(stop);
            repl.backend(2)->admin_connection()->cmd(stop);
            mxs.wait_for_monitor();
            mxs.check_print_servers_status({slave_st, maint_st, mxt::ServerInfo::DRAINED | running_st,
                                            down_st});
            expect_read_only(0, false);
            expect_read_only(1, false);
            expect_read_only(2, true);

            test.tprintf("Bring server2 out of maintenance. It should get read_only.");
            mxs.maxctrl("clear server server2 maint");
            mxs.wait_for_monitor();
            expect_read_only(1, true);

            test.tprintf("Startup server4, it should get read_only.");
            repl.start_node(stopped_node);
            mxs.wait_for_monitor();
            expect_read_only(3, true);

            mxs.maxctrl("clear server server3 drain");
            repl.backend(1)->admin_connection()->cmd(start);
            repl.backend(2)->admin_connection()->cmd(start);
            mxs.wait_for_monitor();
            mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
        }
    }
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
