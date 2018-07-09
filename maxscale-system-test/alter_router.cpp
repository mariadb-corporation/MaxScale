/**
 * Test runtime modification of router options
 */

#include "testconnections.h"
#include <vector>
#include <iostream>
#include <functional>

#define TEST(a) {#a, a}

void alter_readwritesplit(TestConnections& test)
{
    test.maxscales->wait_for_monitor();

    // Open a connection before and after setting master_failure_mode to fail_on_write
    Connection first = test.maxscales->rwsplit();
    Connection second = test.maxscales->rwsplit();
    Connection third = test.maxscales->rwsplit();
    test.maxscales->wait_for_monitor();

    first.connect();
    test.maxscales->ssh_node_f(0, true, "maxctrl alter service RW-Split-Router master_failure_mode fail_on_write");
    second.connect();

    // Check that writes work for both connections
    test.assert(first.query("SELECT @@last_insert_id"),
                "Write to first connection should work: %s", first.error());
    test.assert(second.query("SELECT @@last_insert_id"),
                "Write to second connection should work: %s", second.error());

    // Block the master
    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();

    // Check that reads work for the newer connection and fail for the older one
    test.assert(!first.query("SELECT 1"),
                "Read to first connection should fail.");
    test.assert(second.query("SELECT 1"),
                "Read to second connection should work: %s", second.error());

    // Unblock the master, restart Maxscale and check that changes are persisted
    test.repl->unblock_node(0);
    test.maxscales->wait_for_monitor();
    test.maxscales->restart();

    third.connect();
    test.assert(third.query("SELECT @@last_insert_id"),
                "Write to third connection should work: %s", third.error());

    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();

    test.assert(third.query("SELECT 1"),
                "Read to third connection should work: %s", third.error());

    test.repl->unblock_node(0);
    test.maxscales->wait_for_monitor();
}

void alter_readconnroute(TestConnections& test)
{
    test.repl->connect();
    std::string master_id = test.repl->get_server_id_str(0);
    test.repl->disconnect();

    Connection conn = test.maxscales->readconn_master();

    for (int i = 0; i < 5; i++)
    {
        conn.connect();
        Row row = conn.row("SELECT @@server_id");
        conn.disconnect();
        test.assert(row[0] == master_id, "First connection should use master: %s != %s",
                    row[0].c_str(), master_id.c_str());
    }

    int rc = test.maxscales->ssh_node_f(0, true, "maxctrl alter service Read-Connection-Router-Master router_options slave");
    test.assert(rc == 0, "Readconnroute alteration should work");

    for (int i = 0; i < 5; i++)
    {
        conn.connect();
        Row row = conn.row("SELECT @@server_id");
        conn.disconnect();
        test.assert(row[0] != master_id, "Second connection should not use master: %s == %s",
                    row[0].c_str(), master_id.c_str());
    }
}

void alter_schemarouter(TestConnections& test)
{
    Connection conn = test.maxscales->readconn_slave();
    conn.connect();
    test.assert(!conn.query("SELECT 1"), "Query before reconfiguration should fail");
    conn.disconnect();

    int rc = test.maxscales->ssh_node_f(0, true, "maxctrl alter service SchemaRouter ignore_databases_regex '.*'");
    test.assert(rc == 0, "Schemarouter alteration should work");

    conn.connect();
    test.assert(conn.query("SELECT 1"), "Query after reconfiguration should work: %s", conn.error());
    conn.disconnect();
}

void alter_unsupported(TestConnections& test)
{
    int rc = test.maxscales->ssh_node_f(0, true, "maxctrl alter service RW-Split-Router unknown parameter");
    test.assert(rc != 0, "Unknown router parameter should be detected");
    rc = test.maxscales->ssh_node_f(0, true, "maxctrl alter service RW-Split-Router filters Regex");
    test.assert(rc != 0, "Unsupported router parameter should be detected");
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    std::vector<std::pair<const char*, std::function<void (TestConnections&)>>> tests =
    {
         TEST(alter_readwritesplit),
         TEST(alter_readconnroute),
         TEST(alter_schemarouter),
         TEST(alter_unsupported)
    };

    for (auto& a: tests)
    {
        std::cout << a.first << std::endl;
        a.second(test);
    }

    return test.global_result;
}
