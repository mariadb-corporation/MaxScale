/*
 * Copyright (c) 2025 MariaDB plc
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
#include <maxtest/get_my_ip.hh>
#include <maxbase/stopwatch.hh>

void test_main(TestConnections& test)
{
    char my_ip[1024];
    test.expect(get_my_ip(test.maxscale->ip4(), my_ip) == 0, "Failed to get IP");
    std::string client_user = "'bob'@'"s + my_ip + "'";
    std::string maxscale_user = "'bob'@'"s + test.maxscale->ip4() + "'";
    auto r = test.repl->get_connection(0);
    r.connect();
    test.expect(r.query("CREATE DATABASE IF NOT EXISTS db1"), "Failed to create database: %s", r.error());
    test.expect(r.query("CREATE USER " + client_user + " IDENTIFIED BY 'bob';"
                        + "GRANT ALL ON db1.* TO " + client_user + ";"
                        + "CREATE USER" + maxscale_user + " IDENTIFIED BY 'bob';"),
                "Failed to create user: %s", r.error());

    test.maxscale->start();

    auto c = test.maxscale->rwsplit("db1");
    c.set_credentials("bob", "bob");
    c.connect();
    auto id = std::to_string(c.thread_id());

    bool found;
    auto start = mxb::Clock::now();

    do
    {
        found = false;

        for (auto line : mxb::strtok(test.maxscale->maxctrl("list sessions --tsv").output, "\n"))
        {
            if (mxb::strtok(line, "\t")[0] == id)
            {
                found = true;
            }
        }

        if (found)
        {
            std::this_thread::sleep_for(1s);
        }
    }
    while (found && mxb::Clock::now() - start < 10s);

    test.expect(!found, "The session should close in under 10 seconds");

    r.query("DROP USER " + client_user + "; DROP USER " + maxscale_user);
    r.query("DROP DATABASE db1");
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    return TestConnections().run_test(argc, argv, test_main);
}
