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
    Test->set_timeout(10);
    char result[1024];

    get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show dbusers RW Split Router", (char *) "Incorrect number of arguments:", result);
    Test->tprintf("result %s\n", result);

    if (strstr(result, "show dbusers expects 1 argument") == NULL) {
        Test->add_result(1, "there is NO \"show dbusers expects 1 argument\" message");
    }
    Test->set_timeout(10);
    get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show dbusers \"RW Split Router\"", (char *) "User names:", result);
    Test->tprintf("result %s\n", result);

    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "reload dbusers 0x232fed0");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "reload dbusers Хрен");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "reload dbusers Хрен моржовый");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "Хрен моржовый");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "khren morzhovyj");

    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show Хрен");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show Хрен моржовый");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show khren morzhovyj");

    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show dcb Хрен");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show dcb Хрен моржовый");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show dcb khren morzhovyj");

    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show server Хрен");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show server Хрен моржовый");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show server khren morzhovyj");

    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show service Хрен");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show service Хрен моржовый");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show service khren morzhovyj");

    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show service khren morzhovyj");

    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "list listeners");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "restart monitor");
    Test->set_timeout(10);execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "restart service");

    int N=28;
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

    cmd[14] = "show filters  ";
    cmd[15] = "show modules  ";
    cmd[16] = "show monitors  ";
    cmd[17] = "show servers  ";
    cmd[18] = "show services  ";
    cmd[19] = "show sessions  ";
    cmd[20] = "show tasks  ";
    cmd[21] = "show threads  ";
    cmd[22] = "show users  ";

    cmd[23] = "shutdown monitor ";
    cmd[24] = "shutdown service ";

    cmd[25] = "shutdown maxscale ";

    cmd[26] = "enable root ";
    cmd[27] = "disable root ";

    char str1[4096];
    int i1, i2;

    for (i1 = 0; i1 < N; i1++) {
        for (i2 = 0; i2 < Ng; i2++) {
            Test->set_timeout(10);
            sprintf(str1, "%s %s", cmd[i1], garbage[i2]);
            Test->tprintf("Trying '%s'\n", str1);
            execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, str1);

            sprintf(str1, "%s %s%s%s%s %s ", cmd[i1], garbage[i2], garbage[i2], garbage[i2], garbage[i2], garbage[i2]);
            Test->tprintf("Trying '%s'\n", str1);
            execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, str1);
        }
    }

    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}
