/**
 * @file bug662.cpp regression case for bug 662 ("MaxScale hangs in startup if backend server is not responsive"), covers also bug680 ("RWSplit can't load DB user if backend is not available at MaxScale start")
 *
 * - block all Mariadb servers  Firewall
 * - restart MaxScale
 * - check it took no more then 20 seconds
 * - unblock Mariadb servers
 * - sleep one minute
 * - check if Maxscale is alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);
    int i;

    Test->tprintf("Connecting to Maxscale %s\n", Test->maxscale_IP);

    Test->tprintf("Connecting to Maxscale %s to check its behaviour in case of blocking all bacxkends\n", Test->maxscale_IP);
    Test->connect_maxscale();

    for (i = 0; i < Test->repl->N; i++) {
        Test->tprintf("Setup firewall to block mysql on node %d\n", i);
        Test->repl->block_node(i); fflush(stdout);
    }

    Test->set_timeout(100);
    Test->restart_maxscale();

    Test->set_timeout(20);
    Test->tprintf("Checking if MaxScale is alive by connecting to MaxAdmin\n");
    Test->add_result(execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char* ) "show servers"), "Maxadmin execution failed.\n");

    for (i = 0; i < Test->repl->N; i++) {
        Test->tprintf("Setup firewall back to allow mysql on node %d\n", i);
        Test->repl->unblock_node(i);fflush(stdout);
    }

    Test->stop_timeout();
    Test->tprintf("Sleeping 30 seconds\n");
    sleep(30);

    Test->set_timeout(20);

    Test->tprintf("Checking Maxscale is alive\n");
    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
    //}
}
