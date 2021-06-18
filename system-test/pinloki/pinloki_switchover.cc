#include <maxtest/testconnections.hh>
#include <maxbase/string.hh>
#include "test_base.hh"
#include <iostream>
#include <iomanip>

namespace
{
std::string replicating_from(Connection& conn)
{
    const auto& rows = conn.rows("SHOW SLAVE STATUS");
    return rows[0][1];
}
}

class SwitchoverTest : public TestCase
{
public:
    using TestCase::TestCase;

    void run() override
    {
        switchover();
    }

private:
    void switchover()
    {
        // The initial server setup is:
        // {master, pinloki-replicant, slave, slave, pinloki}
        auto stat_master = mxt::ServerInfo::MASTER | mxt::ServerInfo::RUNNING;
        auto stat_slave = mxt::ServerInfo::SLAVE | mxt::ServerInfo::RUNNING;
        auto stat_ext_slave = mxt::ServerInfo::SERVER_SLAVE_OF_EXT_MASTER | mxt::ServerInfo::RUNNING;
        auto stat_pinloki = mxt::ServerInfo::BLR | mxt::ServerInfo::RUNNING;
        auto initial_stats = {stat_master, stat_ext_slave, stat_slave, stat_slave, stat_pinloki};

        Connection regular_slave {test.repl->get_connection(2)};

        auto master_ip = master.host();
        auto regular_slave_ip = regular_slave.host();   // the second regular slave doesn't come into play

        // Check initial server setup
        auto& mxs = *test.maxscale;
        mxs.wait_for_monitor(2);
        mxs.check_servers_status(initial_stats);

        // Pinloki should be replicating from the master
        auto repl_from = replicating_from(maxscale);
        test.expect(repl_from == master_ip, "Pinloki should replicate from the master");

        // Do switchover to the (first) regular slave
        test.tprintf("Do switchover from %s to %s", master_ip.c_str(), regular_slave_ip.c_str());
        test.maxctrl("call command mysqlmon switchover mariadb-cluster server3 server1");

        // Check that the server setup is as expected
        auto new_stats = {stat_slave, stat_ext_slave, stat_master, stat_slave, stat_pinloki};
        mxs.wait_for_monitor(5);
        mxs.check_servers_status(new_stats);

        // Check that pinloki was redirected
        repl_from = replicating_from(maxscale);
        test.expect(repl_from == regular_slave_ip, "Pinloki should replicate from the switchover master");

        // Kill the new master, the original master should become master again
        test.tprintf("Kill the new master: %s", regular_slave_ip.c_str());
        test.repl->stop_node(2);

        // Check that pinloki was redirected again
        for (int i = 0; i < 60; ++i)
        {
            repl_from = replicating_from(maxscale);
            if (repl_from == master_ip)
            {
                break;
            }
            sleep(1);
        }
        test.expect(repl_from == master_ip, "Pinloki should replicate from the original master");
    }
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return SwitchoverTest(test).result();
}
