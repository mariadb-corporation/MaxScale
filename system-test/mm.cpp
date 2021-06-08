/**
 * @file mm test of mariadbmon in a multimaster situation
 *
 * - use mariadbmon as monitor
 * - reset master, stop slaves, stop all nodes
 * - start 2 nodes
 * - execute SET MASTER TO on node0 to point to node1 and on node1 to point to node0
 * - execute SET GLOBAL READ_ONLY=ON on node0
 * - check server status using maxctrl, expect Master on node1 and Slave on node0
 * - put data to DB using RWSplit, check data using RWSplit and directrly from backend nodes
 * - block node0 (slave)
 * - check server status using maxctrl, expect node0 Down
 * - put data and check it
 * - unblock node0
 * - block node1 (master)
 * - check server status using maxctrl, expect node1 Down
 * - execute SET GLOBAL READ_ONLY=OFF on node0
 * - unblock node0
 * - execute SET GLOBAL READ_ONLY=ON on node1
 * - check server status using maxctrl, expect Master on node0 and Slave on node1
 */


#include <iostream>
#include <maxtest/testconnections.hh>
#include <maxtest/sql_t1.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.repl->set_repl_user();

    test.start_mm();   // first node - slave, second - master

    auto res = test.maxctrl("api get servers/server1 data.attributes.state").output;

    if (strstr(res.c_str(), "Slave, Running") == NULL)
    {
        test.add_result(1, "Node0 is not slave, status is %s\n", res.c_str());
    }

    res = test.maxctrl("api get servers/server2 data.attributes.state").output;

    if (strstr(res.c_str(), "Master, Running") == NULL)
    {
        test.add_result(1, "Node1 is not master, status is %s\n", res.c_str());
    }

    test.tprintf("Block slave\n");
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor();

    res = test.maxctrl("api get servers/server1 data.attributes.state").output;

    if (strstr(res.c_str(), "Down") == NULL)
    {
        test.add_result(1, "Node0 is not down, status is %s\n", res.c_str());
    }

    test.tprintf("Unlock slave\n");
    test.repl->unblock_node(0);
    test.maxscale->wait_for_monitor();

    test.tprintf("Block master\n");
    test.repl->block_node(1);
    test.maxscale->wait_for_monitor();

    res = test.maxctrl("api get servers/server2 data.attributes.state").output;

    if (strstr(res.c_str(), "Down") == NULL)
    {
        test.add_result(1, "Node1 is not down, status is %s\n", res.c_str());
    }
    test.tprintf("Make node 1 master\n");

    test.repl->connect();
    execute_query(test.repl->nodes[0], (char*) "SET GLOBAL READ_ONLY=OFF");
    test.repl->close_connections();

    test.maxscale->wait_for_monitor();

    printf("Unlock slave\n");
    test.repl->unblock_node(1);
    test.maxscale->wait_for_monitor();

    printf("Make node 2 slave\n");
    test.repl->connect();
    execute_query(test.repl->nodes[1], (char*) "SET GLOBAL READ_ONLY=ON");
    test.repl->close_connections();
    test.maxscale->wait_for_monitor();

    res = test.maxctrl("api get servers/server2 data.attributes.state").output;

    if (strstr(res.c_str(), "Slave, Running") == NULL)
    {
        test.add_result(1, "Node1 is not slave, status is %s\n", res.c_str());
    }

    res = test.maxctrl("api get servers/server1 data.attributes.state").output;

    if (strstr(res.c_str(), "Master, Running") == NULL)
    {
        test.add_result(1, "Node0 is not master, status is %s\n", res.c_str());
    }

    return test.global_result;
}
