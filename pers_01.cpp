/**
 * @file pers_01.cpp - Persistent connection tests
 * configuration:
 * @verbatim
[server1]
type=server
address=54.78.193.99
port=3306
protocol=MySQLBackend
persistpoolmax=1
persistmaxtime=3660

[server2]
type=server
address=54.78.254.183
port=3306
protocol=MySQLBackend
persistpoolmax=5
persistmaxtime=60

[server3]
type=server
address=54.78.217.99
port=3306
protocol=MySQLBackend
persistpoolmax=10
persistmaxtime=60

[server4]
type=server
address=176.34.202.107
port=3306
protocol=MySQLBackend
persistpoolmax=30
persistmaxtime=30

@endverbatim
 * open 40 connections to all Maxscale services
 * close connections
 * check value of "Persistent measured pool size" parameter in  'maxadmin' output, expect:
 @verbatim
server1:    1
server2:    5
server3:    10
server4:    30
@endverbatim
 * wait 10 seconds, check "Persistent measured pool size" again. expect the same
 * wait 30 seconds, expect:
@verbatim
server1:    1
server2:    5
server3:    10
server4:    0
@endverbatim
 * wait 30 seconds more, expect:
@verbatim
server1:    1
server2:    0
server3:    0
server4:    0
@endverbatim
 */

#include <my_config.h>
#include "testconnections.h"
#include "maxadmin_operations.h"

void check_pers_conn(TestConnections* Test, int pers_conn_expected[], char * server)
{
    char result[1024];
    char str[256];
    int pers_conn[4];

    for (int i = 0; i < 4; i++) {
        sprintf(str, "show server %s%d", server, i+1);
        get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, str, (char *) "Persistent measured pool size:", result);
        Test->tprintf("%s: %s\n", str, result);
        sscanf(result, "%d", &pers_conn[i]);
        if (pers_conn[i] != pers_conn_expected[i]) {
            Test->add_result(1, "%s%d has %d, but expected %d\n", server, i+1, pers_conn[i], pers_conn_expected[i]);
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

    Test->create_connections(70);

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

    galera_pers_conn_expected[0] = 0;
    galera_pers_conn_expected[1] = 15;
    galera_pers_conn_expected[2] = 0;
    galera_pers_conn_expected[3] = 0;

    check_pers_conn(Test, pers_conn_expected, (char *) "server");

    Test->tprintf("Galera: \n");
    check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");

    Test->tprintf("Sleeping 30 seconds\n");
    Test->stop_timeout();
    sleep(30);
    Test->set_timeout(20);

    Test->tprintf("Test 4:\n");

    pers_conn_expected[0] = 1;
    pers_conn_expected[1] = 0;
    pers_conn_expected[2] = 0;
    pers_conn_expected[3] = 0;

    galera_pers_conn_expected[0] = 0;
    galera_pers_conn_expected[1] = 0;
    galera_pers_conn_expected[2] = 0;
    galera_pers_conn_expected[3] = 0;

    check_pers_conn(Test, pers_conn_expected, (char *) "server");

    Test->tprintf("Galera: \n");
    check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");
    Test->copy_all_logs(); return(Test->global_result);
}
