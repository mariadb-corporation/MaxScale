/**
 * @file slave_failover.cpp  Check how Maxscale works in case of one slave failure
 *
 * - Connect to RWSplit
 * - find which backend slave is used for connection
 * - blocm mariadb on the slave with firewall
 * - wait 60 seconds
 * - check which slave is used for connection now, expecting any other slave
 * - check warning in the error log about broken slave
 * - unblock mariadb backend (restore slave firewall settings)
 * - check if Maxscale still alive
 */

#include <my_config.h>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);
    int res = 0;

    unsigned int current_slave;
    unsigned int old_slave;

    printf("Connecting to RWSplit %s\n", Test->maxscale_IP);
    if (Test->connect_rwsplit() != 0) {
        Test->add_result(1, "Error connection to RWSplit! Exiting\n");
    } else {

        // this is the same test, but with killing VM
        /*    printf("Checking current slave\n");
    old_slave = FindConnectedSlave(Test, &global_result);

    printf("Killing VM\n"); fflush(stdout);
    sprintf(&sys1[0], "%s %s", Test->KillVMCommand, Test->repl->IP[old_slave]);
    system(sys1);
    printf("Sleeping 60 seconds to let MaxScale to find new slave\n");fflush(stdout);
    sleep(60);

    current_slave = FindConnectedSlave(Test, &global_result);
    if ((current_slave == old_slave) || (current_slave < 0)) {printf("FAILED: No failover happened\n"); global_result=1;}


    char err1[1024];

    sprintf(&err1[0], "Error : Monitor was unable to connect to server %s", Test->repl->IP[old_slave]);
    CheckLogErr(err1 , TRUE);

    printf("Starting VM back\n"); fflush(stdout);
    sprintf(&sys1[0], "%s %s", Test->StartVMCommand, Test->repl->IP[old_slave]);
    system(sys1);

    printf("Sleeping 60 seconds to let VM start\n"); fflush(stdout);
    sleep(60);

    printf("Doing test again, but with firewall block instead of VM killing\n");*/

        Test->tprintf("Checking current slave\n");
        old_slave = Test->find_connected_slave( &res);

        Test->add_result(res, "no current slave\n");

        Test->tprintf("Setup firewall to block mysql on old slave (oldslave is node %d)\n", old_slave);
        if ((old_slave < 0) || (old_slave >= Test->repl->N)) {
            Test->add_result(1,"Active slave is not found\n");
        } else {
            Test->repl->block_node(old_slave);

            Test->tprintf("Sleeping 60 seconds to let MaxScale to find new slave\n");
            Test->stop_timeout();
            sleep(60);
            Test->set_timeout(20);

            current_slave = Test->find_connected_slave(&res);
            if ((current_slave == old_slave) || (current_slave < 0)) {Test->add_result(1, "No failover happened\n"); }

            Test->tprintf("Setup firewall back to allow mysql\n");
            Test->repl->unblock_node(old_slave);

            Test->check_maxscale_alive();
            Test->set_timeout(20);

            Test->close_rwsplit();
        }
        Test->set_timeout(200);
        Test->repl->start_replication();
    }

    Test->copy_all_logs(); return(Test->global_result);
}
