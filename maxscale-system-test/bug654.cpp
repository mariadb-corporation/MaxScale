/**
 * @file bug654.cpp  regression case for bug654 abd 698 ("maxadm: show dbusers <two-part service name without quotation> causes SEGFAULT", "Using invalid parameter in many maxadmin commands causes MaxScale to fail")
 *
 * - execute maxadmin command show dbusers RW Split Router and show dbusers "RW Split Router"
 * . execute different maxadmin commands with wrong parameters
 * - check MaxScale is alive
 */

/*
Vilho Raatikka 2014-12-16 13:54:36 UTC
MaxScale> show services
Service 0x1af7eb0
        Service:                                RW Split Router
        Router:                                 readwritesplit (0x7fffdf501440)
        Number of router sessions:              0
        Current no. of router sessions:         0
        Number of queries forwarded:            0
        Number of queries forwarded to master:  0
        Number of queries forwarded to slave:   0
        Number of queries forwarded to all:     0
        Started:                                Tue Dec 16 15:51:54 2014
        Root user access:                       Disabled
        Filter chain:           duplicate
        Backend databases
                127.0.0.1:3003  Protocol: MySQLBackend
                127.0.0.1:3002  Protocol: MySQLBackend
                127.0.0.1:3001  Protocol: MySQLBackend
                127.0.0.1:3000  Protocol: MySQLBackend
        Users data:                             0x1aea000
        Total connections:                      1
        Currently connected:                    1

...

MaxScale> show dbusers RW Split Router

(gdb) bt
#0  0x00007fffdfb4950a in execute_cmd (cli=0x7fffc0000c70) at /home/raatikka/src/git/MaxScale/server/modules/routing/debugcmd.c:805
#1  0x00007fffdfb48ef8 in execute (instance=0x1b0f7b0, router_session=0x7fffc0000c70, queue=0x0) at /home/raatikka/src/git/MaxScale/server/modules/routing/cli.c:279
#2  0x00007ffff46ae934 in maxscaled_read_event (dcb=0x7fffc00009c0) at /home/raatikka/src/git/MaxScale/server/modules/protocol/maxscaled.c:177
#3  0x000000000058b145 in process_pollq (thread_id=2) at /home/raatikka/src/git/MaxScale/server/core/poll.c:858
#4  0x000000000058a7df in poll_waitevents (arg=0x2) at /home/raatikka/src/git/MaxScale/server/core/poll.c:608
#5  0x00007ffff7527e0f in start_thread () from /lib64/libpthread.so.0
#6  0x00007ffff5e0e0dd in clone () from /lib64/libc.so.6
(gdb)
Comment 1 Vilho Raatikka 2014-12-16 13:58:37 UTC
805         for (i = 0; args[i] && *args[i]; i++)

Off-by-one if there are more arguments than expected.
Comment 2 Vilho Raatikka 2014-12-23 16:11:12 UTC
NULL-terminated argument list in case where there are given more arguments than expected.
*/


#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(30);
    char result[1024];

    Test->maxscales->get_maxadmin_param(0, (char *) "show dbusers RW Split Router", (char *) "Incorrect number of arguments:",
                                        result);
    Test->tprintf("result %s\n", result);

    if (strstr(result, "show dbusers expects 1 argument") == NULL)
    {
        Test->add_result(1, "there is NO \"show dbusers expects 1 argument\" message");
    }
    Test->set_timeout(30);
    Test->maxscales->get_maxadmin_param(0, (char *) "show dbusers \"RW Split Router\"", (char *) "User names:", result);
    Test->tprintf("result %s\n", result);

    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "reload dbusers 0x232fed0");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "reload dbusers Хрен");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "reload dbusers Хрен моржовый");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "Хрен моржовый");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "khren morzhovyj");

    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show Хрен");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show Хрен моржовый");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show khren morzhovyj");

    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show dcb Хрен");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show dcb Хрен моржовый");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show dcb khren morzhovyj");

    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show server Хрен");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show server Хрен моржовый");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show server khren morzhovyj");

    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show service Хрен");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show service Хрен моржовый");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show service khren morzhovyj");

    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "show service khren morzhovyj");

    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "list listeners");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "restart monitor");
    Test->set_timeout(30);
    Test->maxscales->execute_maxadmin_command(0, (char *) "restart service");

    if (!Test->smoke)
    {
        int N = 28;
        const char * cmd[N];

        int Ng = 6;
        const char * garbage[Ng];

        garbage[0] = "qwerty";
        garbage[1] = "khren morzhovyj";
        garbage[2] = "Хрен";
        garbage[3] = "Хрен моржовый";
        garbage[4] =
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
        garbage[5] =
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx Хрен моржовый Хрен моржовый ";

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

        for (i1 = 0; i1 < N; i1++)
        {
            for (i2 = 0; i2 < Ng; i2++)
            {
                Test->set_timeout(30);
                sprintf(str1, "%s %s", cmd[i1], garbage[i2]);
                Test->tprintf("Trying '%s'\n", str1);
                Test->maxscales->execute_maxadmin_command(0, str1);

                sprintf(str1, "%s %s%s%s%s %s ", cmd[i1], garbage[i2], garbage[i2], garbage[i2], garbage[i2], garbage[i2]);
                Test->tprintf("Trying '%s'\n", str1);
                Test->maxscales->execute_maxadmin_command(0, str1);
            }
        }
    }
    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
