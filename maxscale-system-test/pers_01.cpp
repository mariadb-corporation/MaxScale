/**
 * @file pers_01.cpp - Persistent connection tests
 * open 70 connections to all Maxscale services
 * close connections
 * TEST1: check value of "Persistent measured pool size" parameter in  'maxadmin' output, expect:
 @verbatim
server1:    1
server2:    5
server3:    10
server4:    30
gserver1:    10
gserver2:    15
gserver3:    0
gserver4:    0
@endverbatim
 * Test2: wait 10 seconds, check "Persistent measured pool size" again. expect the same
 * Test3: wait 30 seconds more, expect:
@verbatim
server1:    1
server2:    5
server3:    10
server4:    0
gserver1:    10
gserver2:    0
gserver3:    0
gserver4:    0
@endverbatim

 */


#include "testconnections.h"
#include "maxadmin_operations.h"

void check_pers_conn(TestConnections* Test, int pers_conn_expected[], char * server)
{
    char result[1024];
    char str[256];
    int pers_conn[4];

    for (int i = 0; i < 4; i++)
    {
        sprintf(str, "show server %s%d", server, i + 1);
        Test->maxscales->get_maxadmin_param(0, str, (char *) "Persistent measured pool size:", result);
        Test->tprintf("%s: %s\n", str, result);
        sscanf(result, "%d", &pers_conn[i]);
        if (pers_conn[i] != pers_conn_expected[i])
        {
            Test->add_result(1, "Persistent measured pool size: %s%d has %d, but expected %d\n", server, i + 1,
                             pers_conn[i], pers_conn_expected[i]);
        }
    }
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int pers_conn_expected[4];
    int galera_pers_conn_expected[4];


    pers_conn_expected[0] = 1;
    pers_conn_expected[1] = 5;
    pers_conn_expected[2] = 10;
    pers_conn_expected[3] = 30;

    galera_pers_conn_expected[0] = 10;
    galera_pers_conn_expected[1] = 15;
    galera_pers_conn_expected[2] = 0;
    galera_pers_conn_expected[3] = 0;

    Test->add_result(Test->create_connections(0, 70, true, true, true, true),
                     "Error creating connections");
    sleep(5);
    Test->set_timeout(20);

    Test->tprintf("Test 1:");
    check_pers_conn(Test, pers_conn_expected, (char *) "server");

    Test->tprintf("Galera: ");
    check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");

    Test->stop_timeout();

    Test->tprintf("Sleeping 10 seconds");
    sleep(10);

    Test->set_timeout(20);
    Test->tprintf("Test 2:");
    check_pers_conn(Test, pers_conn_expected, (char *) "server");

    Test->tprintf("Galera: ");
    check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");

    Test->tprintf("Sleeping 30 seconds");
    Test->stop_timeout();
    sleep(30);

    Test->set_timeout(20);
    Test->tprintf("Test 3:");

    pers_conn_expected[0] = 1;
    pers_conn_expected[1] = 5;
    pers_conn_expected[2] = 10;
    pers_conn_expected[3] = 0;

    galera_pers_conn_expected[0] = 10;
    galera_pers_conn_expected[1] = 0;
    galera_pers_conn_expected[2] = 0;
    galera_pers_conn_expected[3] = 0;

    check_pers_conn(Test, pers_conn_expected, (char *) "server");

    Test->tprintf("Galera: ");
    check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");

    Test->tprintf("Sleeping 30 seconds");
    Test->stop_timeout();
    sleep(30);
    Test->set_timeout(20);

    Test->tprintf("Test 3:");

    pers_conn_expected[0] = 1;
    pers_conn_expected[1] = 0;
    pers_conn_expected[2] = 0;
    pers_conn_expected[3] = 0;

    galera_pers_conn_expected[0] = 10;
    galera_pers_conn_expected[1] = 0;
    galera_pers_conn_expected[2] = 0;
    galera_pers_conn_expected[3] = 0;

    check_pers_conn(Test, pers_conn_expected, (char *) "server");

    Test->tprintf("Galera: ");
    check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");
    int rval = Test->global_result;
    delete Test;
    return rval;
}
