/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <iostream>

using namespace std;
using namespace mxt;

bool prepare(TestConnections& test, MaxScale& maxscale)
{
    // Add entries to /etc/hosts, so that hostnames can be used in
    // the server definitions in the config file.
    mxb_assert(test.xpand);

    XpandCluster& xpand = *test.xpand;

    string hosts("\n");
    for (int i = 0; i < 4; ++i)
    {
        string host("xpand_00");
        host += std::to_string(i);
        string ip = xpand.ip(i);

        hosts += ip + " " + host + "\n";
    }

    int rv = maxscale.ssh_node_f(true, "echo '%s' >> /etc/hosts", hosts.c_str());

    test.expect(rv == 0, "Could not update /etc/hosts");

    return rv == 0;
}

void run(TestConnections& test, MaxScale& maxscale)
{
    if (prepare(test, maxscale))
    {
        test.expect(maxscale.start_and_check_started(), "Could not start MaxScale");

        maxscale.wait_for_monitor(2);

        // If hostnames work, then the servers should not be DOWN.
        for (const ServerInfo& info : maxscale.get_servers())
        {
            test.expect(!(info.status & ServerInfo::DOWN),
                        "Expected %s not to be down, but it is.", info.name.c_str());
        }
    }
}

void test_main(TestConnections& test)
{
    mxb_assert(test.maxscale);

    auto& maxscale = *test.maxscale;

    // Store original /etc/hosts, so that it after the test can be restored.
    int rv = maxscale.ssh_node("cp /etc/hosts /etc/hosts.mxs4750", true);

    if (rv == 0)
    {
        run(test, maxscale);

        rv = maxscale.ssh_node("mv /etc/hosts.mxs4750 /etc/hosts", true);

        test.expect(rv == 0, "Could not restore /etc/hosts");
    }
    else
    {
        test.expect(false, "Could not stash /etc/hosts");
    }
}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);

    return TestConnections().run_test(argc, argv, test_main);
}
