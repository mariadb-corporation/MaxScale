/**
 * @file pers_01.cpp
 *
 * @verbatim
@endverbatim

 */

#include <my_config.h>
#include "testconnections.h"
#include "maxadmin_operations.h"

int check_pers_conn(TestConnections* Test)
{
    char result[1024];
    char str[256];
    int pers_conn[4];
    int pers_conn_expected[4];
    int global_result = 0;

    pers_conn_expected[0] = 1;
    pers_conn_expected[1] = 5;
    pers_conn_expected[2] = 10;
    pers_conn_expected[3] = 100;

    int i;
    for (i = 0; i < 4; i++) {
        sprintf(str, "show server server%d", i+1);
        get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, str, (char *) "Persistent measured pool size:", result);

        printf("%s: %s\n", str, result);fflush(stdout);

        sscanf(result, "%d", &pers_conn[i]);

        if (pers_conn[i] != pers_conn_expected[i]) {
            printf("TEST_FAILED: server%d has %d, but expected %d\n", i+1, pers_conn[i], pers_conn_expected[i]);fflush(stdout);
            global_result++;
        }
    }
    return(global_result);
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int i;


    int conn_N = 100;

    MYSQL * rwsplit_conn[conn_N];
    MYSQL * master_conn[conn_N];
    MYSQL * slave_conn[conn_N];


    printf("Opening 100 connections to each backend\n");
    for (i = 0; i < conn_N; i++) {
        rwsplit_conn[i] = Test->open_rwsplit_connection();
        if (mysql_errno(rwsplit_conn[i]) != 0) {
            printf("%s\n", mysql_error(rwsplit_conn[i]));
        }
        master_conn[i] = Test->open_readconn_master_connection();
        slave_conn[i] = Test->open_readconn_slave_connection();
    }
    printf("Closing all connections\n");
    for (i=0; i<100; i++) {
        mysql_close(rwsplit_conn[i]);
        mysql_close(master_conn[i]);
        mysql_close(slave_conn[i]);
    }

    global_result += check_pers_conn(Test);

    printf("Sleeping 10 seconds\n");
    sleep(10);

    global_result += check_pers_conn(Test);
}
