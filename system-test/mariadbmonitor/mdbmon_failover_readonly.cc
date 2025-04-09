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
#include "mariadbmon_utils.hh"
#include <string>
#include <vector>

using std::string;

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    // Test uses 2 slaves, stop the last one to prevent it from replicating anything.
    test.repl->stop_node(3);

    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    auto mon_wait = [&test](int ticks) {
        test.maxscale->wait_for_monitor(ticks);
    };

    auto crash_node = [&test](int node) {
        auto rc = test.repl->ssh_node(node, "kill -s 11 `pidof mariadbd`", true);
        test.repl->stop_node(node);     // To prevent autostart.
        test.expect(rc == 0, "Kill failed.");
    };

    auto expect_status = [&mxs](const std::vector<mxt::ServerInfo::bitfield>& expected_status,
                                const std::vector<bool>& expected_ro){
        auto status = mxs.get_servers();
        status.print();
        status.check_servers_status(expected_status);
        status.check_read_only(expected_ro);
    };

    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    auto down = mxt::ServerInfo::DOWN;

    // Advance gtid:s a bit to so gtid variables are updated.
    auto maxconn = mxs.open_rwsplit_connection2("test");
    generate_traffic_and_check(test, maxconn.get(), 1);

    test.tprintf("Step 1: All should be cool.");
    expect_status({master, slave, slave}, {false, true, true});

    if (test.ok())
    {
        test.tprintf("Step 2: Crash slave 2.");
        crash_node(2);
        mon_wait(1);
        expect_status({master, slave, down}, {false, true});
        generate_traffic_and_check(test, maxconn.get(), 1);

        test.tprintf("Step 2.1: Slave 2 comes back up, check that read_only is set.");
        repl.start_node(2);
        mon_wait(2);
        expect_status({master, slave, slave}, {false, true, true});
        generate_traffic_and_check(test, maxconn.get(), 2);

        test.tprintf("Step 3: Slave 1 crashes.");
        crash_node(1);
        mon_wait(1);
        expect_status({master, down, slave}, {false, true, true});
        generate_traffic_and_check(test, maxconn.get(), 2);

        test.tprintf("Step 4: Slave 2 goes down again, this time normally.");
        repl.stop_node(2);
        mon_wait(1);
        mxs.check_print_servers_status({master, down, down});
        generate_traffic_and_check(test, maxconn.get(), 2);

        test.tprintf("Step 4.1: Slave 1 comes back up, check that read_only is set.");
        repl.start_node(1);
        mon_wait(2);
        expect_status({master, slave, down}, {false, true});
        generate_traffic_and_check(test, maxconn.get(), 2);

        test.tprintf("Step 4.2: Slave 2 is back up, all should be well.");
        repl.start_node(2);
        mon_wait(2);
        expect_status({master, slave, slave}, {false, true, true});
        generate_traffic_and_check(test, maxconn.get(), 2);
    }
    maxconn.reset();

    // Intermission, quit if a test step failed.
    if (test.ok())
    {
        // Some of the following tests depend on manipulating backends during the same monitor tick or
        // between ticks. Slow down the monitor to make this more likely. Not fool-proof in the slightest.
        test.check_maxctrl("alter monitor MariaDB-Monitor monitor_interval=4000ms");

        test.tprintf("Step 5: Master crashes but comes back during the next loop,"
                     " slave 1 should be promoted, old master rejoined.");
        crash_node(0);
        mon_wait(1);    // The timing is probably a bit iffy here.
        mxs.check_print_servers_status({down});
        repl.start_node(0);
        mon_wait(2);
        // Slave 2 could be promoted as well, but in this case there is no reason to choose it.
        expect_status({slave, master, slave}, {true, false, true});
        maxconn = mxs.open_rwsplit_connection2();
        generate_traffic_and_check(test, maxconn.get(), 2);

        test.tprintf("Step 6: Servers 1 & 3 go down. Server 2 should remain as master.");
        repl.stop_node(0);
        repl.stop_node(2);
        mon_wait(1);
        mxs.check_print_servers_status({down, master, down});
        generate_traffic_and_check(test, maxconn.get(), 2);

        test.tprintf("Step 6.1: Servers 1 & 3 come back. Check that read_only is set.");
        repl.start_node(2);
        repl.start_node(0);
        mon_wait(2);
        expect_status({slave, master, slave}, {true, false, true});
        generate_traffic_and_check(test, maxconn.get(), 2);

        test.tprintf("Step 7: Servers 1 & 2 go down. Check that 3 is promoted.");
        repl.stop_node(0);
        repl.stop_node(1);
        mon_wait(2);
        mxs.check_print_servers_status({down, down, master});
        maxconn = mxs.open_rwsplit_connection2();
        generate_traffic_and_check(test, maxconn.get(), 2);
    }


    // Start the servers, in case they weren't on already.
    for (int i = 0; i < 3; i++)
    {
        repl.start_node(i);
    }
    sleep(1);

    // Delete the test table from all databases, reset replication.
    const string drop_query = "DROP TABLE IF EXISTS test.t1;";
    repl.ping_or_open_admin_connections();
    for (int i = 0; i < 3; i++)
    {
        auto conn = repl.backend(i)->open_connection();
        repl.backend(i)->admin_connection()->cmd(drop_query);
    }
    test.maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
    mon_wait(1);
    mxs.check_print_servers_status({master, slave, slave});
}
