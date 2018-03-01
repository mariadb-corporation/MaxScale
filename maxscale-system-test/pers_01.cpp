/**
 * @file pers_01.cpp - Persistent connection tests
 * configuration:
 * @verbatim
[server1]
type=server
address=###node_server_IP_1###
port=###node_server_port_1###
protocol=MySQLBackend
persistpoolmax=1
persistmaxtime=3660

[server2]
type=server
address=###node_server_IP_2###
port=###node_server_port_2###
protocol=MySQLBackend
persistpoolmax=5
persistmaxtime=60

[server3]
type=server
address=###node_server_IP_3###
port=###node_server_port_3###
protocol=MySQLBackend
persistpoolmax=10
persistmaxtime=60

[server4]
type=server
address=###node_server_IP_4###
port=###node_server_port_4###
protocol=MySQLBackend
persistpoolmax=30
persistmaxtime=30

[gserver1]
type=server
address=###galera_server_IP_1###
port=###galera_server_port_1###
protocol=MySQLBackend
persistpoolmax=10
persistmaxtime=3660

[gserver2]
type=server
address=###galera_server_IP_2###
port=###galera_server_port_2###
protocol=MySQLBackend
persistpoolmax=15
persistmaxtime=30

[gserver3]
type=server
address=###galera_server_IP_3###
port=###galera_server_port_3###
protocol=MySQLBackend
persistpoolmax=19
persistmaxtime=0

[gserver4]
type=server
address=###galera_server_IP_4###
port=###galera_server_port_4###
protocol=MySQLBackend
persistpoolmax=0
persistmaxtime=3660


@endverbatim
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
        Test->get_maxadmin_param(str, (char *) "Persistent measured pool size:", result);
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
    Test->set_timeout(30);
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

    Test->restart_maxscale();

    Test->add_result(Test->create_connections(70, true, true, true, true),
                     "Error creating connections\n");
    sleep(5);
    Test->set_timeout(20);

    Test->tprintf("Test 1:\n");
    check_pers_conn(Test, pers_conn_expected, (char *) "server");

    Test->tprintf("Galera: \n");
    check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");

    Test->stop_timeout();

    Test->tprintf("Sleeping 10 seconds\n");
    sleep(10);

    Test->set_timeout(20);
    Test->tprintf("Test 2:\n");
    check_pers_conn(Test, pers_conn_expected, (char *) "server");

    printf("Galera: \n");
    check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");

    Test->tprintf("Sleeping 30 seconds\n");
    Test->stop_timeout();
    sleep(30);

    Test->set_timeout(20);
    printf("Test 3:\n");

    pers_conn_expected[0] = 1;
    pers_conn_expected[1] = 5;
    pers_conn_expected[2] = 10;
    pers_conn_expected[3] = 0;

    galera_pers_conn_expected[0] = 10;
    galera_pers_conn_expected[1] = 0;
    galera_pers_conn_expected[2] = 0;
    galera_pers_conn_expected[3] = 0;

    check_pers_conn(Test, pers_conn_expected, (char *) "server");

    Test->tprintf("Galera: \n");
    check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");

    Test->tprintf("Sleeping 30 seconds\n");
    Test->stop_timeout();
    sleep(30);
    Test->set_timeout(20);

    Test->tprintf("Test 3:\n");

    pers_conn_expected[0] = 1;
    pers_conn_expected[1] = 0;
    pers_conn_expected[2] = 0;
    pers_conn_expected[3] = 0;

    galera_pers_conn_expected[0] = 10;
    galera_pers_conn_expected[1] = 0;
    galera_pers_conn_expected[2] = 0;
    galera_pers_conn_expected[3] = 0;

    check_pers_conn(Test, pers_conn_expected, (char *) "server");

    Test->tprintf("Galera: \n");
    check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");
    int rval = Test->global_result;
    delete Test;
    return rval;
}
