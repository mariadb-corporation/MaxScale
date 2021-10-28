/**
 * @file galera_priority.cpp Galera node priority test
 *
 * Node priorities are configured in the following order:
 * node3 > node1 > node4 > node2
 *
 * The test executes a SELECT @@server_id to get the server id of each
 * node. The same query is executed in a transaction through MaxScale
 * and the server id should match the expected output depending on which
 * of the nodes are available. The simple test blocks nodes from highest priority
 * to lowest priority.
 */

#include <maxbase/format.hh>
#include <maxtest/testconnections.hh>
#include <maxtest/galera_cluster.hh>

void check_server_id(TestConnections& test, const std::string& id)
{
    test.tprintf("Expecting '%s'...", id.c_str());
    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection should work: %s", conn.error());
    test.expect(conn.query("BEGIN"), "BEGIN should work: %s", conn.error());
    auto f = conn.field("SELECT @@server_id");
    test.expect(f == id, "Expected server_id '%s', not server_id '%s'", id.c_str(), f.c_str());
    test.expect(conn.query("COMMIT"), "BEGIN should work: %s", conn.error());
}

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& galera = *test.galera;
    auto& mxs = *test.maxscale;

    galera.connect();
    auto ids = galera.get_all_server_ids_str();

    /** Node 3 should be master */
    check_server_id(test, ids[2]);

    /** Block node 3 and node 1 should be master */
    galera.block_node(2);
    mxs.wait_for_monitor(2);
    check_server_id(test, ids[0]);

    /** Block node 1 and node 4 should be master */
    galera.block_node(0);
    mxs.wait_for_monitor(2);
    check_server_id(test, ids[3]);

    /** Block node 4 and node 2 should be master */
    galera.block_node(3);
    mxs.wait_for_monitor(2);
    check_server_id(test, ids[1]);

    /** All nodes blocked, expect failure */
    galera.block_node(1);
    mxs.wait_for_monitor(2);

    auto conn = mxs.rwsplit();
    test.expect(!conn.connect(), "Connecting to rwsplit should fail");

    /** Unblock all nodes, node 3 should be master again */
    galera.unblock_all_nodes();
    mxs.wait_for_monitor(2);
    check_server_id(test, ids[2]);

    /** Restart MaxScale check that states are the same */
    mxs.restart();
    mxs.wait_for_monitor(2);
    check_server_id(test, ids[2]);

    if (test.ok())
    {
        // Test MXS-3826: Galera master can be set to maintenance, which leads to master change.
        auto MASTER = mxt::ServerInfo::MASTER;
        auto MAINT = mxt::ServerInfo::MAINT;

        const char no_master[] = "No master in cluster.";
        const char no_change[] = "Master did not change.";
        const char no_maint[] = "Server not in maintenance.";

        const char set_maint[] = "set server %s Maint";
        const char clear_maint[] = "clear server %s Maint";

        auto orig_info = mxs.get_servers();
        orig_info.print();
        auto orig_master = orig_info.get_master();
        test.expect(orig_master.status & MASTER, no_master);

        if (test.ok())
        {
            test.tprintf("Set master to maintenance, check that monitor changes master.");
            auto cmd = mxb::string_printf(set_maint, orig_master.name.c_str());
            mxs.maxctrl(cmd);
            mxs.wait_for_monitor(2);
            auto second_info = mxs.get_servers();
            second_info.print();
            auto second_master = second_info.get_master();
            test.expect(second_info.get(orig_master.name).status & MAINT, no_maint);
            test.expect(second_master.status & MASTER, no_master);
            test.expect(second_master.server_id != orig_master.server_id, no_change);

            if (test.ok())
            {
                test.tprintf("Again...");
                cmd = mxb::string_printf(set_maint, second_master.name.c_str());
                mxs.maxctrl(cmd);
                mxs.wait_for_monitor(2);
                auto third_info = mxs.get_servers();
                third_info.print();
                auto third_master = third_info.get_master();
                test.expect(third_info.get(second_master.name).status & MAINT, no_maint);
                test.expect(third_master.status & MASTER, no_master);
                test.expect((third_master.server_id != second_master.server_id)
                            && (third_master.server_id != orig_master.server_id), no_change);

                cmd = mxb::string_printf(clear_maint, second_master.name.c_str());
                mxs.maxctrl(cmd);
            }

            cmd = mxb::string_printf(clear_maint, orig_master.name.c_str());
            mxs.maxctrl(cmd);
        }

        if (test.ok())
        {
            // Finally, check that the master cannot be set to draining mode (MXS-3532).
            auto info = mxs.get_servers();
            auto master = info.get_master();
            if (master.status & MASTER)
            {
                test.tprintf("Trying to set %s to drain, it should fail.", master.name.c_str());
                const char set_drain[] = "set server %s drain";
                auto cmd = mxb::string_printf(set_drain, master.name.c_str());
                auto res = mxs.maxctrl(cmd);
                test.expect(res.rc != 0, "Command '%s' succeeded when it should have failed.", cmd.c_str());
                mxs.wait_for_monitor(2);
                auto info_after = mxs.get_servers();
                info_after.print();
                auto master_status = info_after.get(master.name).status;
                const auto drain_bits = mxt::ServerInfo::DRAINING | mxt::ServerInfo::DRAINED;
                test.expect((master_status & drain_bits) == 0,
                            "%s was set to draining/drained when it should not have.", master.name.c_str());

                if (test.ok())
                {
                    test.tprintf("Check that a slave can be set to draining.");
                    std::string slave_name;
                    for (size_t i = 0; i < info_after.size(); i++)
                    {
                        if (info_after.get(i).status & mxt::ServerInfo::SLAVE)
                        {
                            slave_name = info_after.get(i).name;
                            break;
                        }
                    }

                    if (!slave_name.empty())
                    {
                        cmd = mxb::string_printf(set_drain, slave_name.c_str());
                        mxs.maxctrl(cmd);
                        mxs.wait_for_monitor(1);
                        info_after = mxs.get_servers();
                        info_after.print();
                        auto slave_status = info_after.get(slave_name).status;
                        test.expect(slave_status & drain_bits,
                                    "%s is not draining/drained when it should be.", slave_name.c_str());
                        mxs.maxctrl(mxb::string_printf("clear server %s drain", slave_name.c_str()));
                    }
                    else
                    {
                        test.add_failure("No slaves in cluster.");
                    }
                }
            }
            else
            {
                test.add_failure(no_master);
            }
        }
    }
}
