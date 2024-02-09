/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-2350: On-demand connection creation
 * https://jira.mariadb.org/browse/MXS-2350
 */

#include <maxtest/testconnections.hh>
#include <maxtest/maxrest.hh>
#include <maxbase/string.hh>

std::map<std::string, int> get_connections(TestConnections& test)
{
    std::map<std::string, int> rval;
    MaxRest api(&test);

    for (auto srv : api.list_servers())
    {
        rval[srv.name] = srv.connections;
    }

    return rval;
}

template<class Map>
int sum(const Map& m)
{
    int n = 0;

    for (const auto& [k, v] : m)
    {
        n += v;
    }

    return n;
}

template<class Map>
std::string print(const Map& m)
{
    std::ostringstream ss;

    for (const auto& [k, v] : m)
    {
        ss << k << " " << v << "\t";
    }

    return ss.str();
}

void mxs4776_normal_sescmd(TestConnections& test)
{
    Connection c = test.maxscale->rwsplit();
    c.connect();

    auto conns = get_connections(test);
    test.expect(sum(conns) == 0, "Sum of all connections should be 0: %s", print(conns).c_str());

    // Session command should be treated as a read
    c.query("SET NAMES utf8mb3");

    conns = get_connections(test);
    test.expect(conns["server1"] == 0, "server1 should have 0 connections: %s", print(conns).c_str());
    test.expect(sum(conns) == 1, "Sum of all connections should be 1: %s", print(conns).c_str());

    // Reads should get load balanced across all nodes
    for (int i = 0; i < 100; i++)
    {
        c.query("SELECT 1");
    }

    conns = get_connections(test);
    test.expect(conns["server1"] == 0, "server1 should have 0 connections: %s", print(conns).c_str());
    test.expect(sum(conns) == 3, "Sum of all connections should be 3: %s", print(conns).c_str());
}

void mxs4776_max_slave_connections(TestConnections& test)
{
    test.check_maxctrl("alter service RW-Split-Router master_accept_reads=true");
    Connection c = test.maxscale->rwsplit();
    c.connect();

    auto conns = get_connections(test);
    test.expect(sum(conns) == 0, "Sum of all connections should be 0: %s", print(conns).c_str());

    // Session command should be treated as a write
    c.query("SET NAMES utf8mb3");

    conns = get_connections(test);
    test.expect(conns["server1"] == 1, "server1 should have 1 connection: %s", print(conns).c_str());
    test.expect(sum(conns) == 1, "Sum of all connections should be 1: %s", print(conns).c_str());

    // Reads should get load balanced across all nodes, including the master
    for (int i = 0; i < 100; i++)
    {
        c.query("SELECT 1");
    }

    conns = get_connections(test);
    test.expect(sum(conns) == 4, "Sum of all connections should be 4: %s", print(conns).c_str());

    test.check_maxctrl("alter service RW-Split-Router master_accept_reads=false");
}

void mxs4776_master_accept_reads(TestConnections& test)
{
    test.check_maxctrl("alter service RW-Split-Router max_slave_connections=1");
    Connection c = test.maxscale->rwsplit();
    c.connect();

    auto conns = get_connections(test);
    test.expect(sum(conns) == 0, "Sum of all connections should be 0: %s", print(conns).c_str());

    // Session command should be treated as a read
    c.query("SET NAMES utf8mb3");

    conns = get_connections(test);
    test.expect(conns["server1"] == 0, "server1 should have 0 connections: %s", print(conns).c_str());
    test.expect(sum(conns) == 1, "Sum of all connections should be 1: %s", print(conns).c_str());

    // Reads should get load balanced across all nodes
    for (int i = 0; i < 100; i++)
    {
        c.query("SELECT 1");
    }

    conns = get_connections(test);
    test.expect(sum(conns) == 1, "Sum of all connections should still be 1: %s", print(conns).c_str());

    test.check_maxctrl("alter service RW-Split-Router max_slave_connections=256");
}

// The session may end up in an infinite retry loop if lazy_connect is used and authentication fails on all
// backends while a session command is being routed. This is not strictly related to lazy_connect but it
// happens much more often if it's enabled.
void mxs4956(TestConnections& test)
{
    // Turn on delayed_retry
    test.check_maxctrl("alter service RW-Split-Router delayed_retry=true delayed_retry_timeout=5s "
                       "master_failure_mode=fail_on_write master_reconnection=true log_info=true");

    Connection admin = test.maxscale->rwsplit();
    admin.connect();

    // Create a user for the test
    admin.query("CREATE USER mxs4956_user IDENTIFIED BY 'mxs4965'");
    admin.query("GRANT ALL ON *.* TO mxs4956_user");
    test.repl->sync_slaves();

    // lazy_connect should delay the creation of the connection until the first query arrives.
    Connection c = test.maxscale->rwsplit();
    c.set_timeout(60);
    c.set_credentials("mxs4956_user", "mxs4965");
    test.expect(c.connect(), "Failed to connect: %s", c.error());

    // Drop the user and then execute a session command. The time it takes it to fail should be below the
    // configured test timeout.
    admin.query("DROP USER mxs4956_user");
    test.repl->sync_slaves();
    auto start = std::chrono::steady_clock::now();
    test.expect(!c.query("SET NAMES latin1"), "Query with dropped user should fail");
    auto end = std::chrono::steady_clock::now();
    test.expect(end - start < 30s, "Query should fail in under 30 seconds");
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    Connection c = test.maxscale->rwsplit();

    test.expect(c.connect(), "Connection should work");
    auto output = test.maxscale->ssh_output("maxctrl list servers --tsv|cut -f 4|sort|uniq").output;
    mxb::trim(output);
    test.expect(output == "0", "Servers should have no connections: %s", output.c_str());
    c.disconnect();

    test.expect(c.connect(), "Connection should work");
    test.expect(c.query("SELECT 1"), "Read should work");
    c.disconnect();

    test.expect(c.connect(), "Connection should work");
    test.expect(c.query("SELECT @@last_insert_id"), "Write should work");
    c.disconnect();

    test.expect(c.connect(), "Connection should work");
    test.expect(c.query("SET @a = 1"), "Session command should work");
    c.disconnect();

    test.expect(c.connect(), "Connection should work");
    test.expect(c.query("BEGIN"), "BEGIN should work");
    test.expect(c.query("SELECT 1"), "Read should work");
    test.expect(c.query("COMMIT"), "COMMIT command should work");
    c.disconnect();

    test.expect(c.connect(), "Connection should work");
    test.expect(c.query("SET @a = 1"), "Session command should work");

    test.repl->block_all_nodes();
    test.maxscale->wait_for_monitor();
    test.repl->unblock_all_nodes();
    test.maxscale->wait_for_monitor();

    test.expect(c.query("SET @a = 1"), "Session command should work: %s", c.error());
    c.disconnect();

    // MXS-4776: Sescmd target selection is sub-optimal with lazy_connect
    // https://jira.mariadb.org/browse/MXS-4776
    mxs4776_normal_sescmd(test);
    mxs4776_master_accept_reads(test);
    mxs4776_max_slave_connections(test);

    test.tprintf("MXS-4956: Session commands ignore delayed_retry_timeout");
    mxs4956(test);


    return test.global_result;
}
