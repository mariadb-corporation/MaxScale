/**
 * @file bug662.cpp regression case for bug 662 ("MaxScale hangs in startup if backend server is not
 * responsive"), covers also bug680 ("RWSplit can't load DB user if backend is not available at MaxScale
 * start")
 *
 * - Block all Mariadb servers
 * - Restart MaxScale
 * - Unblock Mariadb servers
 * - Sleep and check if Maxscale is alive
 */

#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->maxscales->connect_maxscale(0);

    for (int i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(30);
        Test->tprintf("Setup firewall to block mysql on node %d\n", i);
        Test->repl->block_node(i);
    }

    Test->set_timeout(200);
    Test->tprintf("Restarting MaxScale");
    Test->maxscales->restart_maxscale(0);

    Test->tprintf("Checking if MaxScale is alive by connecting to MaxAdmin\n");
    Test->add_result(Test->maxscales->execute_maxadmin_command(0, (char*) "show servers"),
                     "Maxadmin execution failed.\n");

    for (int i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(30);
        Test->tprintf("Setup firewall back to allow mysql on node %d\n", i);
        Test->repl->unblock_node(i);
    }

    sleep(3);

    Test->set_timeout(30);
    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
