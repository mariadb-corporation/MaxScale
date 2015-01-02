/**
 * @file bug654.cpp  regression case for bug654 ("maxadm: show dbusers <two-part service name without quotation> causes SEGFAULT")
 *
 * - execute maxadmin command show dbusers RW Split Router and show dbusers "RW Split Router"
 * - check MaxScale is alive
 */

#include <my_config.h>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main()
{

    TestConnections * Test = new TestConnections();
    int global_result = 0;
    char result[1024];

    Test->ReadEnv();
    Test->PrintIP();

    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dbusers RW Split Router", (char *) "Incorrect number of arguments:", result);
    printf("result %s\n", result);

    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dbusers \"RW Split Router\"", (char *) "User names:", result);
    printf("result %s\n", result);


    global_result += CheckMaxscaleAlive();

    exit(global_result);
}
