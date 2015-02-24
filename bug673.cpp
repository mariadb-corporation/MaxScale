/**
 * @file bug673.cpp  regression case for bug673 ("MaxScale crashes if "Users table data" is empty and "show dbusers" is executed in maxadmin")
 *
 * - execute maxadmin command show dbusers "RW Split Router"
 * - check MaxScale is alive by executing maxadmin again
 */

#include <my_config.h>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = 0;
    char result[1024];

    Test->ReadEnv();
    Test->PrintIP();

    sleep(150);

    printf("Trying show dbusers \"RW Split Router\"\n"); fflush(stdout);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dbusers \"RW Split Router\"", (char *) "No. of entries:", result);
    printf("result %s\n", result); fflush(stdout);

    printf("Trying show dbusers \"Read Connection Router Master\"\n"); fflush(stdout);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dbusers \"Read Connection Router Master\"", (char *) "No. of entries:", result);
    printf("result %s\n", result); fflush(stdout);


    printf("Trying show dbusers \"Read Connection Router Slave\"\n"); fflush(stdout);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dbusers \"Read Connection Router Slave\"", (char *) "No. of entries:", result);
    printf("result %s\n", result); fflush(stdout);


    printf("Trying again show dbusers \"RW Split Router\" to check if MaxScale is alive\n"); fflush(stdout);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dbusers \"RW Split Router\"", (char *) "No. of entries:", result);
    printf("result %s\n", result);

    /*int users_num = 1;
    sscanf(result, "%d", &users_num);
    if (users_num != 0) {
        printf("FAULT: result is not 0\n");
        global_result++;
    }*/

    Test->Copy_all_logs(); return(global_result);
}
