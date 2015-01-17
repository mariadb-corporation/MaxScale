/**
 * @file bug654.cpp  regression case for bug654 abd 698 ("maxadm: show dbusers <two-part service name without quotation> causes SEGFAULT", "Using invalid parameter in many maxadmin commands causes MaxScale to fail")
 *
 * - execute maxadmin command show dbusers RW Split Router and show dbusers "RW Split Router"
 * . execute different maxadmin commands with wrong parameters
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

    if (strstr(result, "show dbusers expects 1 argument") == NULL) {
        printf("FAULT: there is NO \"show dbusers expects 1 argument\" message");
        global_result++;
    }
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dbusers \"RW Split Router\"", (char *) "User names:", result);
    printf("result %s\n", result);

    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "reload dbusers 0x232fed0", (char *) "User names:", result);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "reload dbusers Хрен", (char *) "User names:", result);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "reload dbusers Хрен моржовый", (char *) "User names:", result);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "Хрен моржовый", (char *) "User names:", result);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "khren morzhovyj", (char *) "User names:", result);

    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show Хрен", (char *) "User names:", result);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show Хрен моржовый", (char *) "User names:", result);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show khren morzhovyj", (char *) "User names:", result);

    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dcb Хрен", (char *) "User names:", result);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dcb Хрен моржовый", (char *) "User names:", result);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dcb khren morzhovyj", (char *) "User names:", result);

    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show server Хрен", (char *) "User names:", result);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show server Хрен моржовый", (char *) "User names:", result);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show server khren morzhovyj", (char *) "User names:", result);

    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show service Хрен", (char *) "User names:", result);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show service Хрен моржовый", (char *) "User names:", result);
    getMaxadminParam(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show service khren morzhovyj", (char *) "User names:", result);


    global_result += CheckMaxscaleAlive();

    exit(global_result);
}
