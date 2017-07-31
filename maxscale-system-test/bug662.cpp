/**
 * @file bug662.cpp regression case for bug 662 ("MaxScale hangs in startup if backend server is not responsive"), covers also bug680 ("RWSplit can't load DB user if backend is not available at MaxScale start")
 *
 * - Block all Mariadb servers
 * - Restart MaxScale
 * - Unblock Mariadb servers
 * - Sleep and check if Maxscale is alive
 */

#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int i;

    Test->tprintf("Connecting to Maxscale %s\n", Test->maxscale_IP);

    Test->tprintf("Connecting to Maxscale %s to check its behaviour in case of blocking all backends\n",
                  Test->maxscale_IP);
    Test->connect_maxscale();

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(30);
        Test->tprintf("Setup firewall to block mysql on node %d\n", i);
        Test->repl->block_node(i);
        fflush(stdout);
    }

    Test->set_timeout(200);
    Test->tprintf("Restarting MaxScale");
    Test->restart_maxscale();

    Test->tprintf("Checking if MaxScale is alive by connecting to MaxAdmin\n");
    Test->add_result(Test->execute_maxadmin_command((char* ) "show servers"), "Maxadmin execution failed.\n");

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(30);
        Test->tprintf("Setup firewall back to allow mysql on node %d\n", i);
        Test->repl->unblock_node(i);
        fflush(stdout);
    }

    Test->stop_timeout();
    Test->tprintf("Sleeping 30 seconds\n");
    sleep(30);

    Test->set_timeout(30);
    Test->tprintf("Checking Maxscale is alive\n");
    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
