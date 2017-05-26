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
    test.ssh_maxscale(true, "maxadmin clear server server1 master");
    sleep(5);

    test.repl->unblock_node(0);
    sleep(5);

    test.check_maxscale_alive();
    test.check_log_err("debug assert", false);

    return test.global_result;
}
