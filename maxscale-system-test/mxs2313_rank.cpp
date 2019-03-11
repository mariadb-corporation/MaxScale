/**
 * MXS-2313: `rank` functional tests
 * https://jira.mariadb.org/browse/MXS-2313
 */

#include "testconnections.h"
#include <iostream>

std::function<void(int)> block_wait;
std::function<void(int)> unblock_wait;

void test_rwsplit(TestConnections& test, std::vector<std::string> ids)
{
    std::cout << "Slaves with descending rank and a low ranking master" << std::endl;

    test.check_maxctrl("alter server server1 rank 9999");
    test.check_maxctrl("alter server server2 rank 2");
    test.check_maxctrl("alter server server3 rank 3");
    test.check_maxctrl("alter server server4 rank 4");

    Connection c = test.maxscales->rwsplit();
    c.connect();
    test.expect(c.field("SELECT @@server_id") == ids[1], "First slave should reply");

    block_wait(1);
    test.expect(c.field("SELECT @@server_id") == ids[2], "Second slave should reply");

    block_wait(2);
    test.expect(c.field("SELECT @@server_id") == ids[3], "Third slave should reply");

    block_wait(3);
    test.expect(c.field("SELECT @@server_id") == ids[0], "Master should reply");

    block_wait(0);
    test.expect(!c.query("SELECT @@server_id"), "Query should fail");

    unblock_wait(0);
    c.disconnect();
    c.connect();
    test.expect(c.field("SELECT @@server_id") == ids[0], "Master should reply");

    unblock_wait(3);
    test.expect(c.field("SELECT @@server_id") == ids[3], "Third slave should reply");

    unblock_wait(2);
    test.expect(c.field("SELECT @@server_id") == ids[2], "Second slave should reply");

    unblock_wait(1);
    test.expect(c.field("SELECT @@server_id") == ids[1], "First slave should reply");

    std::cout << "Grouping servers into a three-node cluster with one low-ranking server" << std::endl;

    test.check_maxctrl("alter server server1 rank 1");
    test.check_maxctrl("alter server server2 rank 1");
    test.check_maxctrl("alter server server3 rank 1");
    test.check_maxctrl("alter server server4 rank 9999");

    block_wait(0);
    auto id = c.field("SELECT @@server_id");
    test.expect(!id.empty() && id != ids[3], "Third slave should not reply");

    block_wait(1);
    id = c.field("SELECT @@server_id");
    test.expect(!id.empty() && id != ids[3], "Third slave should not reply");

    block_wait(2);
    test.expect(c.field("SELECT @@server_id") == ids[3], "Third slave should reply");

    for (int i = 0; i < 3; i++)
    {
        unblock_wait(i);
        auto id = c.field("SELECT @@server_id");
        test.expect(!id.empty() && id != ids[3], "Third slave should not reply");
    }
}

void test_readconnroute(TestConnections& test, std::vector<std::string> ids)
{
    std::cout << "Readconnroute with descending server rank" << std::endl;

    test.check_maxctrl("alter server server1 rank 1");
    test.check_maxctrl("alter server server2 rank 2");
    test.check_maxctrl("alter server server3 rank 3");
    test.check_maxctrl("alter server server4 rank 4");

    auto do_test = [&](int node) {
            Connection c = test.maxscales->readconn_master();
            c.connect();
            test.expect(c.field("SELECT @@server_id") == ids[node], "server%d should reply", node + 1);
        };

    do_test(0);
    block_wait(0);
    do_test(1);
    block_wait(1);
    do_test(2);
    block_wait(2);
    do_test(3);
    unblock_wait(2);
    do_test(2);
    unblock_wait(1);
    do_test(1);
    unblock_wait(0);
    do_test(0);
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    block_wait = [&](int node) {
            std::cout << "Block server" << (node + 1) << std::endl;
            test.repl->block_node(node);
            test.maxscales->wait_for_monitor(2);
        };
    unblock_wait = [&](int node) {
            std::cout << "Unblock server" << (node + 1) << std::endl;
            test.repl->unblock_node(node);
            test.maxscales->wait_for_monitor(2);
        };

    test.repl->connect();
    auto ids = test.repl->get_all_server_ids_str();
    test.repl->disconnect();

    test_rwsplit(test, ids);
    test_readconnroute(test, ids);

    return test.global_result;
}
