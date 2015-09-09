/**
 * @file pers_01.cpp
 *
 * @verbatim
@endverbatim

 */

#include <my_config.h>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int i;
    int pers_conn[4];
    int pers_conn_expected[4];

    pers_conn_expected[0] = 1;
    pers_conn_expected[1] = 5;
    pers_conn_expected[2] = 10;
    pers_conn_expected[3] = 100;

    for (i=0; i<100; i++) {
        if (Test->connect_maxscale() != 0) {
            printf("TEST_FAILED: connection error\n");
            global_result++;
        }
        Test->close_maxscale_connections();
    }
    sleep(10);

    char result[1024];
    char str[256];

    for (i = 0; i < 4; i++) {
        sprintf(str, "show server server%d", i+1);
        get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, str, (char *) "Persistent measured pool size:", result);

        printf("%s\n", result);

        sscanf(result, "%d", &pers_conn[i]);

        if (pers_conn[i] != pers_conn_expected[i]) {
            printf("TEST_FAILED: server%d has %d, but expected %d", i+1, pers_conn[i], pers_conn_expected[i]);
        }
    }

}
