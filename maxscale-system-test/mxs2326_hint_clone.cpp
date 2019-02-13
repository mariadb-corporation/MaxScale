/**
 * MXS-2326: Routing hints aren't cloned in gwbuf_clone
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    Connection c = test.maxscales->rwsplit();
    test.expect(c.connect(), "Connection should work: %s", c.error());

    std::string correct_id = c.field("SELECT @@server_id -- maxscale route to server server4");

    test.tprintf("Executing session command");
    test.expect(c.query("SET @a = 1"), "SET should work: %s", c.error());

    test.tprintf("Forcing a reconnection to occur on the next query by blocking the server");
    test.repl->block_node(3);
    test.maxscales->wait_for_monitor();
    test.repl->unblock_node(3);
    test.maxscales->wait_for_monitor();

    test.tprintf("Executing a query with a routing hint to a server that the session is not connected to");
    test.expect(c.check("SELECT @@server_id -- maxscale route to server server4",
                        correct_id), "Hint should be routed to the same server");

    return test.global_result;
}
