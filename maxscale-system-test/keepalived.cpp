/**
 * @file keepalived.cpp keepalived Test of two Maxscale + keepalived failover
 *
 * - 'version_string' configured to be different for every Maxscale
 * - configure keepalived for two nodes (uses xxx.xxx.xxx.253 as a virtual IP
 * where xxx.xxx.xxx. - first 3 numbers from client IP)
 * - suspend Maxscale 1
 * - wait and check version_string from Maxscale on virtual IP, expect 10.2-server2
 * - resume Maxscale 1, suspend Maxscale 2
 * - wait and check version_string from Maxscale on virtual IP, expect 10.2-server1
 * - resume Maxscale 2
 * TODO: replace 'yum' call with executing Chef recipe
 */


#include <iostream>
#include "testconnections.h"
#include "keepalived_func.h"

int main(int argc, char *argv[])
{

    char * version;

    TestConnections::multiple_maxscales(true);
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->tprintf("Maxscale_N %d\n", Test->maxscales->N);
    if (Test->maxscales->N < 2)
    {
        Test->tprintf("At least 2 Maxscales are needed for this test. Exiting\n");
        exit(0);
    }


    Test->check_maxscale_alive(0);
    Test->check_maxscale_alive(1);

    // Get test client IP, replace last number in it with 253 and use it as Virtual IP
    configure_keepalived(Test, (char *) "");

    print_version_string(Test);

    Test->tprintf("Suspend Maxscale 000 machine and waiting\n");
    Test->add_result(Test->maxscales->start_vm(0), "Failed to stop VM maxscale_000\n");
    sleep(FAILOVER_WAIT_TIME);

    version = print_version_string(Test);
    if (strcmp(version, "10.2-server2") != 0)
    {
        Test->add_result(false, "Failover did not happen");
    }


    Test->tprintf("Resume Maxscale 000 machine and waiting\n");
    Test->add_result(Test->maxscales->start_vm(0), "Failed to start VM maxscale_000\n");
    sleep(FAILOVER_WAIT_TIME);
    print_version_string(Test);

    Test->tprintf("Suspend Maxscale 001 machine and waiting\n");
    Test->add_result(Test->maxscales->start_vm(1), "Failed to stop VM maxscale_001\n");
    sleep(FAILOVER_WAIT_TIME);

    version = print_version_string(Test);
    if (strcmp(version, "10.2-server1") != 0)
    {
        Test->add_result(false, "Failover did not happen");
    }

    print_version_string(Test);
    Test->tprintf("Resume Maxscale 001 machine and waiting\n");
    Test->add_result(Test->maxscales->start_vm(1), "Failed to start VM maxscale_001\n");
    sleep(FAILOVER_WAIT_TIME);
    print_version_string(Test);

    Test->tprintf("Stop Maxscale on 000 machine\n");
    Test->stop_maxscale(0);
    sleep(FAILOVER_WAIT_TIME);
    version = print_version_string(Test);
    if (strcmp(version, "10.2-server2") != 0)
    {
        Test->add_result(false, "Failover did not happen");
    }

    Test->tprintf("Start back Maxscale on 000 machine\n");
    Test->start_maxscale(0);
    sleep(FAILOVER_WAIT_TIME);

    Test->tprintf("Stop Maxscale on 001 machine\n");
    Test->stop_maxscale(1);
    sleep(FAILOVER_WAIT_TIME);
    version = print_version_string(Test);
    if (strcmp(version, "10.2-server1") != 0)
    {
        Test->add_result(false, "Failover did not happen");
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}

