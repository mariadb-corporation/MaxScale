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

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
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

     executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "reload dbusers 0x232fed0");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "reload dbusers Хрен");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "reload dbusers Хрен моржовый");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "Хрен моржовый");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "khren morzhovyj");

    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show Хрен");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show Хрен моржовый");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show khren morzhovyj");

    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dcb Хрен");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dcb Хрен моржовый");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show dcb khren morzhovyj");

    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show server Хрен");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show server Хрен моржовый");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show server khren morzhovyj");

    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show service Хрен");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show service Хрен моржовый");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show service khren morzhovyj");

    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "show service khren morzhovyj");

    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "list listeners");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "restart monitor");
    executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", (char *) "restart service");


    int N=27;
    const char * cmd[N];

    int Ng=6;
    const char * garbage[Ng];

    garbage[0] = "qwerty";
    garbage[1] = "khren morzhovyj";
    garbage[2] = "Хрен";
    garbage[3] = "Хрен моржовый";
    garbage[4] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    garbage[5] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx Хрен моржовый Хрен моржовый ";

    cmd[0] = "enable disable heartbeat ";
    cmd[1] = "disable heartbeat ";
    cmd[2] = "reload dbusers ";

    cmd[3] = "set server server1 master  ";

    cmd[4] = "set pollsleep  ";
    cmd[5] = "set nbpolls  ";

    cmd[6] = "show dcb ";
    cmd[7] = "show eventq ";
    cmd[8] = "show eventstats ";
    cmd[9] = "show filter ";
    cmd[10] = "show monitor ";
    cmd[11] = "show server ";
    cmd[12] = "show service ";
    cmd[13] = "show session ";


    cmd[13] = "show filters  ";
    cmd[14] = "show modules  ";
    cmd[15] = "show monitors  ";
    cmd[16] = "show servers  ";
    cmd[17] = "show services  ";
    cmd[18] = "show sessions  ";
    cmd[19] = "show tasks  ";
    cmd[20] = "show threads  ";
    cmd[21] = "show users  ";


    cmd[22] = "shutdown monitor ";
    cmd[23] = "shutdown service ";

    cmd[24] = "shutdown maxscale ";

    cmd[25] = "enable root ";
    cmd[26] = "disable root ";

    char str1[4096];
    int i1, i2;

    for (i1 = 0; i1 < N; i1++) {
        for (i2 = 0; i2 < Ng; i2++) {
            sprintf(str1, "%s %s", cmd[i1], garbage[i2]);
            printf("Trying '%s'\n", str1); fflush(stdout);
            executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", str1);

            sprintf(str1, "%s %s%s%s%s %s ", cmd[i1], garbage[i2], garbage[i2], garbage[i2], garbage[i2], garbage[i2]);
            printf("Trying '%s'\n", str1); fflush(stdout);
            executeMaxadminCommand(Test->Maxscale_IP, (char *) "admin", (char *) "skysql", str1);
        }
    }


    global_result += CheckMaxscaleAlive();

    Test->Copy_all_logs(); return(global_result);
}
