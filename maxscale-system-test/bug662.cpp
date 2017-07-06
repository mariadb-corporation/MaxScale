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

/*
Vilho Raatikka 2014-12-29 08:38:28 UTC
During startup, load_mysql_users tries to read the contents of the mysql.user table. If the chosen backend is not responsive, connection hangs for a long time.
Comment 1 Vilho Raatikka 2014-12-29 11:41:32 UTC
The issue causes long stalls for the executing thread whenever getUsers function is called and one or more backends are not responsive.
Comment 2 Vilho Raatikka 2014-12-29 11:50:10 UTC
dbusers.c: Added function for setting read, write and connection timeout values. Set default timeouts for getUsers. Defaults are listed in service.c
    gateway.c:shutdown_server is called whenever MaxScale is to be shut down. Added call for service_shutdown to shutdown_server.
    service.c:service_alloc: replaced malloc with calloc and removed unnecessary zero/NULL initialization statements as a consequence.
        serviceStart: Exit serviceStartPort loop if shutdown flag is set for the service.
        serviceStartAll: Exit serviceStart loop if shutdown flag is set for the service.
    service.c: Added service_shutdown which sets shutdown flag for each service found in allServices list.
    service.h: Added prototype for service_shutdown
*/



#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);
    int i;

    Test->tprintf("Connecting to Maxscale %s\n", Test->maxscale_IP);

    Test->tprintf("Connecting to Maxscale %s to check its behaviour in case of blocking all backends\n",
                  Test->maxscale_IP);
    Test->connect_maxscale();

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->tprintf("Setup firewall to block mysql on node %d\n", i);
        Test->repl->block_node(i);
        fflush(stdout);
    }

    Test->set_timeout(200);
    Test->tprintf("Restarting MaxScale");
    Test->restart_maxscale();

    Test->set_timeout(20);
    Test->tprintf("Checking if MaxScale is alive by connecting to MaxAdmin\n");
    Test->add_result(Test->execute_maxadmin_command((char* ) "show servers"), "Maxadmin execution failed.\n");

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->tprintf("Setup firewall back to allow mysql on node %d\n", i);
        Test->repl->unblock_node(i);
        fflush(stdout);
    }

    Test->stop_timeout();
    Test->tprintf("Sleeping 30 seconds\n");
    sleep(30);

    Test->set_timeout(20);

    Test->tprintf("Checking Maxscale is alive\n");
    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
    //}
}
