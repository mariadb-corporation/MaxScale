/**
 * @file stale_slaves.cpp Testing slaves who have lost their master and how MaxScale works with them
 *
 * When the master server is blocked and slaves lose their master, they should
 * still be available for read queries. Once the master comes back, all slaves
 * should get slave status if replication is running.
 */

#include "testconnections.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace std;

int main(int argc, char **argv)
{
    TestConnections test(argc, argv);
    vector<string> ids;

    test.repl->connect();
    for (int i = 0; i < test.repl->N; i++)
    {
        ids.push_back(test.repl->get_server_id_str(i));
    }

    auto get_id = [&]()
    {
        Connection c = test.maxscales->readconn_slave();
        test.assert(c.connect(), "Connection should be OK: %s", c.error());
        string res = c.field("SELECT @@server_id");
        test.assert(!res.empty(), "Field should not be empty: %s", c.error());
        return res;
    };

    auto in_use = [&](string id)
    {
        for (int i = 0; i < 2 * test.repl->N; i++)
        {
            if (get_id() == id)
            {
                return true;
            }
        }

        return false;
    };

    test.tprintf("Blocking the master and doing a read query");
    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();

    string first = get_id();
    auto it = find(begin(ids), end(ids), first);
    test.assert(it != end(ids), "ID should be found");
    int node = distance(begin(ids), it);

    test.tprintf("Blocking the slave that replied to us");
    test.repl->block_node(node);
    test.maxscales->wait_for_monitor();
    test.assert(!in_use(first), "The first slave should not be in use");

    test.tprintf("Unblocking all nodes");
    test.repl->unblock_all_nodes();
    test.maxscales->wait_for_monitor();
    test.assert(in_use(first), "The first slave should be in use");

    test.tprintf("Stopping replication on first slave");
    execute_query(test.repl->nodes[node], "STOP SLAVE");
    test.maxscales->wait_for_monitor();
    test.assert(!in_use(first), "The first slave should not be in use");

    test.tprintf("Starting replication on first slave");
    execute_query(test.repl->nodes[node], "START SLAVE");
    test.maxscales->wait_for_monitor();
    test.assert(in_use(first), "The first slave should be in use");
    test.repl->disconnect();

    return test.global_result;
}
