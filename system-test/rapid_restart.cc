/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <algorithm>
#include <fstream>

const int TOTAL = 100;
const int RESTARTS = 10;
const int PORT_START = 5000;

std::vector<std::thread> open_connections(TestConnections& test)
{
    std::vector<std::thread> threads;

    for (int i = 0; i < TOTAL; i++)
    {
        threads.emplace_back([&, i](){
            auto c = test.maxscale->get_connection(PORT_START + i);
            test.expect(c.connect(), "Failed to connect: %s", c.error());

            while (c.query("SELECT 1"))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }

    return threads;
}

void test_main(TestConnections& test)
{
    const auto& user = test.maxscale->user_name();
    const auto& password = test.maxscale->password();

    test.tprintf("Creating %d services and monitors", TOTAL);

    std::ofstream commands("commands.txt");

    for (int i = 0; i < TOTAL; i++)
    {
        commands
            << "create server srv-" << i
            << " port=" << test.repl->port(0)
            << " address=" << test.repl->ip(0)
            << "\n"
            << "create monitor mon-" << i << " mariadbmon"
            << " user=" << user << " password=" << password << " monitor_interval=100ms"
            << " servers=srv-" << i
            << "\n"
            << "create service svc-" << i << " readwritesplit"
            << " user=" << user << " password=" << password
            << " cluster=mon-" << i
            << "\n"
            << "create listener svc-" << i << " listener-" << i << " " << (PORT_START + i)
            << "\n";
    }

    commands.flush();
    test.maxscale->copy_to_node("./commands.txt", "/tmp/commands.txt");
    test.check_maxctrl(" < /tmp/commands.txt");

    test.tprintf("Restarting MaxScale %d times", RESTARTS);

    for (int i = 0; i < RESTARTS; i++)
    {
        auto thr = open_connections(test);
        test.maxscale->restart();
        std::for_each(thr.begin(), thr.end(), std::mem_fn(&std::thread::join));
    }
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
