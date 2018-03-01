/**
 * @file mxs1045.cpp Regression case for the bug "Defunct processes after maxscale have executed script during failover"
 * - configure monitor:
 * @verbatim
script=/bin/sh -c "echo hello world!"
events=master_down,server_down

 * @endverbatim
 * - block one node
 * - Check that script execution doesn't leave zombie processes
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Block master");
    test.repl->block_node(0);

    test.tprintf("Wait for monitor to see it");
    sleep(10);

    test.tprintf("Check that there are no zombies");

    int res = test.ssh_maxscale(false,
                                "if [ \"`ps -ef|grep defunct|grep -v grep`\" != \"\" ]; then exit 1; fi");
    test.add_result(res, "Zombie processes were found");

    test.repl->unblock_node(0);

    return test.global_result;
}
