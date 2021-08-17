/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxbase/string.hh>
#include <maxscale/jansson.hh>
#include <maxtest/maxrest.hh>
#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

using namespace std;

void check_login(TestConnections& test);

namespace
{

const set<string> bootstrap_servers =
{
    "xpand_server1",
    "xpand_server2",
    "xpand_server3",
    "xpand_server4",
};

const std::string monitor_name = "Xpand-Monitor";

void expect_all_servers_to_be(const MaxRest& maxrest, const std::string& state)
{
    cout << "Expecting the state of all servers to be: " << state << endl;

    TestConnections& test = maxrest.test();
    auto servers = maxrest.list_servers();

    for (const auto& server : servers)
    {
        cout << server.name << "(" << server.address << "): " << server.state << endl;
        test.expect(server.state.find(state) != string::npos,
                    "State of %s(%s) is '%s', expected '%s.",
                    server.name.c_str(),
                    server.address.c_str(),
                    server.state.c_str(),
                    state.c_str());
    }
}

void expect_server_to_be(const MaxRest& maxrest, const MaxRest::Server& server, const std::string& state)
{
    TestConnections& test = maxrest.test();
    cout << "Expecting the state of '" << server.name << "' to be '" << state << "'." << endl;

    test.expect(server.state.find(state) != string::npos,
                "State of '%s' was not '%s', but '%s'.",
                server.name.c_str(),
                state.c_str(),
                server.state.c_str());
}

void check_for_servers(const MaxRest& maxrest)
{
    TestConnections& test = maxrest.test();

    auto servers = maxrest.list_servers();

    test.expect(servers.size() >= bootstrap_servers.size(),
                "Expected at least %d servers.", (int)bootstrap_servers.size());

    set<string> static_servers;
    set<string> dynamic_servers;

    string prefix = "@@" + monitor_name;

    for (const auto& server : servers)
    {
        string name = server.name;

        cout << "Looking at: " << name << endl;

        if (bootstrap_servers.find(name) != bootstrap_servers.end())
        {
            static_servers.insert(name);
            continue;
        }

        if (name.find(prefix) != 0)
        {
            test.expect(false, "The name of a dynamic Xpand node does not start with \"%s\": %s",
                        prefix.c_str(), name.c_str());
        }

        dynamic_servers.insert(name);
    }

    test.expect(static_servers == bootstrap_servers,
                "Did not find expected servers.\n"
                "Found   : %s\n"
                "Expected: %s",
                mxb::join(static_servers).c_str(),
                mxb::join(bootstrap_servers).c_str());

    test.expect(dynamic_servers.size() == 4,
                "Did not find expected numbers of servers %d != 4: %s",
                (int)dynamic_servers.size(),
                mxb::join(dynamic_servers).c_str());
}

void check_state_change(const MaxRest& maxrest)
{
    TestConnections& test = maxrest.test();

    // The Xpand-monitor depends on the internal monitor of the Xpand-cluster itself. Since it has a delay,
    // some sleeps are required when expecting state changes.
    const int cycles = 4;
    test.maxscale->sleep_and_wait_for_monitor(cycles, cycles);
    expect_all_servers_to_be(maxrest, "Master, Running");
    cout << endl;

    int node = 0;
    string address = test.xpand->ip_private(node);

    cout << "Blocking node: " << node << endl;
    test.xpand->block_node(node);

    cout << "Waiting for " << cycles << " monitor cycles." << endl;
    test.maxscale->sleep_and_wait_for_monitor(cycles, cycles);

    auto servers = maxrest.list_servers();

    for (const auto& server : servers)
    {
        cout << server.name << "(" << server.address << "): " << server.state << endl;
        if (server.address == address)
        {
            test.expect(server.state == "Down",
                        "Blocked server was not 'Down' but '%s'.", server.state.c_str());
        }
    }

    cout << endl;

    test.xpand->unblock_node(node);
    cout << "Waiting for " << cycles << " monitor cycles." << endl;
    test.maxscale->sleep_and_wait_for_monitor(cycles, cycles);

    expect_all_servers_to_be(maxrest, "Master, Running");
    cout << endl;
}

void check_softfailing(const MaxRest& maxrest)
{
    TestConnections& test = maxrest.test();

    // We'll softfail the node with the largest nid. Any node would do,
    // but for repeatability the same should be selected each time.
    auto servers = maxrest.list_servers();
    string id;
    int max_nid = -1;

    for (const auto& server : servers)
    {
        auto i = server.name.find_last_of("-");
        auto s = server.name.substr(i + 1);
        int nid = atoi(s.c_str());

        if (nid > max_nid)
        {
            id = server.name;
            max_nid = nid;
        }
    }

    MaxRest::Server before = maxrest.show_server(id);
    expect_server_to_be(maxrest, before, "Master, Running");

    cout << "Softfailing " << id << "." << endl;
    maxrest.call_command("xpandmon", "softfail", monitor_name, {id});

    MaxRest::Server during = maxrest.show_server(id);
    expect_server_to_be(maxrest, during, "Drained");

    cout << "Unsoftfailing " << id << "." << endl;
    maxrest.call_command("xpandmon", "unsoftfail", monitor_name, {id});

    MaxRest::Server after = maxrest.show_server(id);
    expect_server_to_be(maxrest, after, "Master, Running");
}

void run_test(TestConnections& test)
{
    MaxRest maxrest(&test);
    check_login(test);
    check_for_servers(maxrest);
    check_state_change(maxrest);
    check_softfailing(maxrest);
}
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    try
    {
        run_test(test);
    }
    catch (const std::exception& x)
    {
        cout << "Exception: " << x.what() << endl;
    }

    return test.global_result;
}

void check_login(TestConnections& test)
{
    test.maxscale->stop();
    test.xpand->connect();
    auto conn = test.xpand->nodes[0];
    const char svc_user[] = "rwsplit_user";
    const char svc_user_host[] = "'rwsplit_user'@'%'";
    const char svc_pw[] = "rwsplit_pw";

    const char drop_fmt[] = "DROP USER %s;";
    const char create_fmt[] = "CREATE USER %s IDENTIFIED BY '%s';";

    string drop_query = mxb::string_printf(drop_fmt, svc_user_host);
    execute_query_silent(conn, drop_query.c_str());
    test.try_query(conn, create_fmt, svc_user_host, svc_pw);
    test.try_query(conn, "GRANT SELECT ON system.membership TO %s;", svc_user_host);
    test.try_query(conn, "GRANT SELECT ON system.nodeinfo TO %s;", svc_user_host);
    test.try_query(conn, "GRANT SELECT ON system.softfailed_nodes TO %s;", svc_user_host);
    test.try_query(conn, "GRANT SUPER ON *.* TO %s;", svc_user_host);

    const char db_user[] = "tester1";
    const char db_user_host[] = "'tester1'@'%'";
    const char db_pw[] = "tester1_pw";

    drop_query = mxb::string_printf(drop_fmt, db_user_host);
    execute_query_silent(conn, drop_query.c_str());
    test.try_query(conn, create_fmt, db_user_host, db_pw);
    test.try_query(conn, "GRANT SELECT ON test.* TO %s;", db_user_host);

    const char no_db_user[] = "tester2";
    const char no_db_user_host[] = "'tester2'@'%'";
    const char no_db_pw[] = "tester2_pw";

    drop_query = mxb::string_printf(drop_fmt, no_db_user_host);
    execute_query_silent(conn, drop_query.c_str());
    test.try_query(conn, create_fmt, no_db_user_host, no_db_pw);

    sleep(1);
    test.maxscale->start();
    sleep(1);

    auto test_login = [&](const char* user, const char* pw, const char* db, bool expect_success) {
            int port = test.maxscale->rwsplit_port;
            auto ip = test.maxscale->ip();

            MYSQL* rwsplit_conn = db ? open_conn_db(port, ip, db, user, pw) :
                open_conn_no_db(port, ip, user, pw);

            if (expect_success)
            {
                test.expect(mysql_errno(rwsplit_conn) == 0, "RWSplit connection failed: '%s'",
                            mysql_error(rwsplit_conn));
                if (test.ok())
                {
                    test.try_query(rwsplit_conn, "select rand();");
                    test.tprintf("%s logged in and queried", user);
                }
            }
            else
            {
                test.expect(mysql_errno(rwsplit_conn) != 0,
                            "RWSplit connection succeeded when failure was expected");
            }
            mysql_close(rwsplit_conn);
        };

    if (test.ok())
    {
        test_login(svc_user, svc_pw, nullptr, true);
    }
    if (test.ok())
    {
        test_login(db_user, db_pw, "test", true);
    }

    /*
     *  if (test.ok())
     *  {
     *   // TODO: this case should fail if the db grant check for Xpand would work. Fix later.
     *   test_login(no_db_user, no_db_pw, "test", true);
     *  }
     */
    test.try_query(conn, drop_fmt, svc_user_host);
    test.try_query(conn, drop_fmt, db_user_host);
    test.try_query(conn, drop_fmt, no_db_user_host);
}
