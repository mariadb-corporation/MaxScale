/*
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
#include <maxbase/format.hh>

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;

    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    auto set_repl_mode = [&repl](const char* ip_host, int port, int skip_ind) {
        for (int i = 0; i < repl.N; i++)
        {
            if (i != skip_ind)
            {
                auto be = repl.backend(i);
                auto conn = be->admin_connection();
                conn->cmd("stop slave;");
                conn->cmd_f("change master to master_host='%s', master_port=%i, master_user='repl', "
                            "master_password='repl', master_use_gtid=slave_pos;",
                            ip_host, port);
                conn->cmd("start slave;");
            }
        }
    };

    auto expect_repl_host = [&test, &repl](const char* ip_host, int skip_ind) {
        for (int i = 0; i < repl.N; i++)
        {
            if (i != skip_ind)
            {
                auto be = repl.backend(i);
                auto conn = be->admin_connection();
                auto res = conn->query("show all slaves status;");
                if (res && res->next_row())
                {
                    auto host = res->get_string("Master_Host");
                    test.tprintf("Master_Host of %s is %s", be->cnf_name().c_str(), host.c_str());
                    test.expect(host == ip_host, "Wrong Master_Host. Found %s, expected %s.",
                                host.c_str(), ip_host);
                }
                else
                {
                    test.add_failure("No slave connections.");
                }
            }
        }
    };
    int master_ind = 0;
    auto master_be = repl.backend(master_ind);
    set_repl_mode(master_be->vm_node().hostname(), master_be->port(), master_ind);
    mxs.wait_for_monitor(1);
    expect_repl_host(master_be->vm_node().hostname(), master_ind);

    // Server states should be ok as monitor does name lookup. This test doesn't properly test that private
    // address is detected separately, as testing that requires another network interface on servers.
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    // Change back to normal replication and check.
    set_repl_mode(master_be->vm_node().ip4(), master_be->port(), master_ind);
    mxs.wait_for_monitor(1);
    expect_repl_host(master_be->vm_node().ip4(), master_ind);

    if (test.ok())
    {
        // Set up private addresses and do a switchover.
        for (int i = 0; i < repl.N; i++)
        {
            auto cmd = mxb::string_printf("alter server server%i private_address %s",
                                          i + 1, repl.backend(i)->vm_node().hostname());
            auto res = mxs.maxctrl(cmd);
            test.expect(res.rc == 0, "alter server failed: %s", res.output.c_str());
        }
        sleep(1);

        test.check_maxctrl("call command mariadbmon switchover MariaDB-Monitor server2");
        mxs.sleep_and_wait_for_monitor(1, 1);
        mxs.check_print_servers_status({slave, master, slave, slave});

        master_ind = 1;
        master_be = repl.backend(master_ind);
        expect_repl_host(master_be->vm_node().hostname(), master_ind);

        // Disable private addresses and reset situation.
        for (int i = 0; i < repl.N; i++)
        {
            auto cmd = mxb::string_printf("alter server server%i private_address=''", i + 1);
            auto res = mxs.maxctrl(cmd);
            test.expect(res.rc == 0, "alter server failed: %s", res.output.c_str());
        }
        sleep(1);
        test.check_maxctrl("call command mariadbmon switchover MariaDB-Monitor server1");
        mxs.sleep_and_wait_for_monitor(1, 1);

        master_ind = 0;
        master_be = repl.backend(master_ind);
        expect_repl_host(master_be->vm_node().ip4(), master_ind);
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    }
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
