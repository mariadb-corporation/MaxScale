/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxbase/string.hh>
#include <maxtest/maxrest.hh>
#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

namespace
{

const std::string monitor_name = "Xpand-Monitor";

void test_login(TestConnections& test, int port, const char* user, const char* pw, const char* db)
{
    auto ip = test.maxscale->ip();

    MYSQL* rwsplit_conn = db ? open_conn_db(port, ip, db, user, pw) :
        open_conn_no_db(port, ip, user, pw);

    test.expect(mysql_errno(rwsplit_conn) == 0, "RWSplit connection failed: '%s'",
                mysql_error(rwsplit_conn));
    if (test.ok())
    {
        test.try_query(rwsplit_conn, "select rand();");
        test.tprintf("%s logged in and queried to port %i.", user, port);
    }
    mysql_close(rwsplit_conn);
}

void test_main(TestConnections& test)
{
    auto master = mxt::ServerInfo::master_st;
    auto& mxs = *test.maxscale;
    auto& xpand = *test.xpand;

    mxs.check_print_servers_status({master, master, master, master, master, master, master, master});

    const char drop_fmt[] = "DROP USER %s;";
    const char create_fmt[] = "CREATE USER %s IDENTIFIED BY '%s';";

    const char super_user[] = "super_user";
    const char super_user_host[] = "'super_user'@'%'";
    const char super_pw[] = "super_pw";

    const char db_user[] = "db_user";
    const char db_user_host[] = "'db_user'@'%'";
    const char db_pw[] = "db_pw";

    auto conn = xpand.backend(0)->open_connection();
    conn->try_cmd_f(drop_fmt, super_user_host);
    conn->try_cmd_f(drop_fmt, db_user_host);

    conn->cmd_f(create_fmt, super_user_host, super_pw);
    conn->cmd_f("GRANT SUPER ON *.* TO %s;", super_user_host);
    conn->cmd_f(create_fmt, db_user_host, db_pw);
    conn->cmd_f("GRANT SELECT ON test.* TO %s;", db_user_host);

    int port = 4006;
    test_login(test, port, super_user, super_pw, nullptr);
    test_login(test, port, db_user, db_pw, "test");

    if (test.ok())
    {
        test.tprintf("Creating a service during runtime.");
        // MXS-3934: Services created at runtime don't work with xpandmon
        test.check_maxctrl("create service my-test-service readwritesplit user=maxskysql password=skysql");
        test.check_maxctrl("link service my-test-service Xpand-Monitor");
        test.check_maxctrl("create listener my-test-service my-test-listener 4009");

        test.tprintf("Service created, logging in...");
        port = 4009;
        test_login(test, port, super_user, super_pw, nullptr);
        test_login(test, port, db_user, db_pw, "test");

        // MXS-3938: Should be possible to unlink servers
        if (test.ok())
        {
            test.tprintf("Remove all but one server from monitor, check that monitor status shows the "
                         "effect.");
            test.check_maxctrl("unlink monitor Xpand-Monitor xpand_server2 xpand_server3 xpand_server4");
            auto res = mxs.maxctrl("show monitor Xpand-Monitor --tsv");
            test.expect(res.rc == 0, "MaxCtrl command failed.");

            if (test.ok())
            {
                std::istringstream input(res.output);
                std::string line;
                bool line_found = false;

                while (std::getline(input, line))
                {
                    if (line.find("Servers") == 0)
                    {
                        line_found = true;
                        auto words = mxb::strtok(line, "\t");
                        auto n_servers = words.size() - 1;
                        if (n_servers == 1)
                        {
                            const char expected[] = "xpand_server1";
                            test.expect(words[1] == expected, "Wrong server %s. Expected %s.",
                                        words[1].c_str(), expected);
                        }
                        else
                        {
                            test.add_failure("Wrong number of servers. Expected 1, got %zu.", n_servers);
                        }
                        break;
                    }
                }

                test.expect(line_found, "No 'Servers'-line in MaxCtrl output");
            }
        }

        // Remove the created (if success) dynamic config file, so as not to cause trouble later.
        mxs.ssh_node("rm -f /var/lib/maxscale/maxscale.cnf.d/Xpand-Monitor.cnf", true);
    }

    conn->cmd_f(drop_fmt, super_user_host);
    conn->cmd_f(drop_fmt, db_user_host);
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
