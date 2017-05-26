/**
 * @file slave_failover.cpp  Check how Maxscale works in case of one slave failure, only one slave is configured
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


#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);
    int res = 0;

    unsigned int current_slave;
    unsigned int old_slave;

    printf("Connecting to RWSplit %s\n", Test->maxscale_IP);
    if (Test->connect_rwsplit() != 0)
    {
        Test->add_result(1, "Error connection to RWSplit! Exiting\n");
    }
    else
    {

        Test->tprintf("Checking current slave\n");
        old_slave = Test->find_connected_slave( &res);

        Test->add_result(res, "no current slave\n");

        Test->tprintf("Setup firewall to block mysql on old slave (oldslave is node %d)\n", old_slave);
        if ((old_slave < 0) || (old_slave >= Test->repl->N))
        {
            Test->add_result(1, "Active slave is not found\n");
        }
        else
        {
            Test->repl->block_node(old_slave);

            Test->tprintf("Sleeping 60 seconds to let MaxScale to find new slave\n");
            Test->stop_timeout();
            sleep(60);
            Test->set_timeout(20);

            current_slave = Test->find_connected_slave(&res);
            if ((current_slave == old_slave) || (current_slave < 0))
            {
                Test->add_result(1, "No failover happened\n");
            }

            Test->tprintf("Setup firewall back to allow mysql\n");
            Test->repl->unblock_node(old_slave);

            Test->close_rwsplit();

            Test->check_maxscale_alive();
            Test->set_timeout(20);
        }
        Test->set_timeout(200);
        Test->repl->start_replication();
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}
