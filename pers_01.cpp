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

int check_pers_conn(TestConnections* Test, int pers_conn_expected[], char * server)
{
    char result[1024];
    char str[256];
    int pers_conn[4];

    int global_result = 0;


    int i;
    for (i = 0; i < 4; i++) {
        sprintf(str, "show server %s%d", server, i+1);
        get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, str, (char *) "Persistent measured pool size:", result);

        printf("%s: %s\n", str, result);fflush(stdout);

        sscanf(result, "%d", &pers_conn[i]);

        if (pers_conn[i] != pers_conn_expected[i]) {
            printf("TEST_FAILED: %s%d has %d, but expected %d\n", server, i+1, pers_conn[i], pers_conn_expected[i]);fflush(stdout);
            global_result++;
        }
    }
    return(global_result);
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int pers_conn_expected[4];
    int galera_pers_conn_expected[4];
    int global_result = 0;

    pers_conn_expected[0] = 1;
    pers_conn_expected[1] = 5;
    pers_conn_expected[2] = 10;
    pers_conn_expected[3] = 30;

    galera_pers_conn_expected[0] = 10;
    galera_pers_conn_expected[1] = 15;
    galera_pers_conn_expected[2] = 0;
    galera_pers_conn_expected[3] = 0;


    int i;


    int conn_N = 40;

    MYSQL * rwsplit_conn[conn_N];
    MYSQL * master_conn[conn_N];
    MYSQL * slave_conn[conn_N];
    MYSQL * galera_conn[conn_N];


    printf("Opening 40 connections to each backend\n");
    for (i = 0; i < conn_N; i++) {
        rwsplit_conn[i] = Test->open_rwsplit_connection();
        if (mysql_errno(rwsplit_conn[i]) != 0) {
            printf("%s\n", mysql_error(rwsplit_conn[i]));
        }
        master_conn[i] = Test->open_readconn_master_connection();
        slave_conn[i] = Test->open_readconn_slave_connection();
        galera_conn[i] = open_conn(4016, Test->maxscale_IP, Test->maxscale_user, Test->maxscale_password, Test->ssl);
    }
    for (i = 0; i < conn_N; i++) {
        if (execute_query(rwsplit_conn[i], "select 1;") != 0) {
            printf("Query failed!\n");
        }
        execute_query(master_conn[i], "select 1;");
        execute_query(slave_conn[i], "select 1;");
    }

    //global_result += check_pers_conn(Test, pers_conn_expected);
    printf("Closing all connections\n");
    for (i=0; i<conn_N; i++) {
        mysql_close(rwsplit_conn[i]);
        mysql_close(master_conn[i]);
        mysql_close(slave_conn[i]);
    }

    printf("Test 1:\n");
    global_result += check_pers_conn(Test, pers_conn_expected, (char *) "server");

    printf("Galera: \n");
    global_result += check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");

    printf("Sleeping 10 seconds\n");
    sleep(10);

    printf("Test 2:\n");
    global_result += check_pers_conn(Test, pers_conn_expected, (char *) "server");

    printf("Galera: \n");
    global_result += check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");



    printf("Sleeping 30 seconds\n");
    sleep(30);

    printf("Test 3:\n");

    pers_conn_expected[0] = 1;
    pers_conn_expected[1] = 5;
    pers_conn_expected[2] = 10;
    pers_conn_expected[3] = 0;

    galera_pers_conn_expected[0] = 0;
    galera_pers_conn_expected[1] = 15;
    galera_pers_conn_expected[2] = 0;
    galera_pers_conn_expected[3] = 0;

    global_result += check_pers_conn(Test, pers_conn_expected, (char *) "server");

    printf("Galera: \n");
    global_result += check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");

    printf("Sleeping 30 seconds\n");
    sleep(30);

    printf("Test 4:\n");

    pers_conn_expected[0] = 1;
    pers_conn_expected[1] = 0;
    pers_conn_expected[2] = 0;
    pers_conn_expected[3] = 0;

    galera_pers_conn_expected[0] = 0;
    galera_pers_conn_expected[1] = 0;
    galera_pers_conn_expected[2] = 0;
    galera_pers_conn_expected[3] = 0;

    global_result += check_pers_conn(Test, pers_conn_expected, (char *) "server");

    printf("Galera: \n");
    global_result += check_pers_conn(Test, galera_pers_conn_expected, (char *) "gserver");
}
