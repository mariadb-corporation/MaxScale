/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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
        // The initial server setup, starting from node 0 is:
        // {master, pinloki-replicant, slave, slave, pinloki}

        Connection regular_slave {test.repl->get_connection(2)};

        auto master_ip = master.host();
        auto regular_slave_ip = regular_slave.host();   // the second regular slave doesn't come into play
        auto& mxs = *test.maxscale;

        mxs.wait_for_monitor(2);

        // Pinloki should be replicating from the master
        auto repl_from = replicating_from(maxscale);
        test.expect(repl_from == master_ip, "Pinloki should replicate from the master");

        // Do switchover to the (first) regular slave
        test.tprintf("Do switchover from %s to %s", master_ip.c_str(), regular_slave_ip.c_str());
        test.maxctrl("call command mysqlmon switchover mariadb-cluster server3 server1");

        mxs.wait_for_monitor(5);

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
