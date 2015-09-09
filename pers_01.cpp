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

    for (i=0; i<100; i++) {
        if (Test->connect_maxscale() != 0) {
            printf("TEST_FAILED: connection error\n");
            global_result++;
        }
        Test->close_maxscale_connections();
    }
    sleep(10);

    char result[1024];
    get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show server server1", (char *) "“Persistent measured pool size:", result);

    printf("%s\n", result);

}
