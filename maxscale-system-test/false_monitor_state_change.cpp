/**
 * Test false server state changes when manually clearing master bit
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Block master");
    test.repl->block_node(0);

    test.tprintf("Wait for monitor to see it");
    sleep(10);

    test.tprintf("Clear master status");
    test.maxscales->ssh_node(0, "maxadmin clear server server1 master", true);
    sleep(5);

    test.repl->unblock_node(0);
    sleep(5);

    test.check_maxscale_alive(0);
    test.check_log_err(0, "debug assert", false);

    return test.global_result;
}
