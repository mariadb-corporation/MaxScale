
/**
 * @file mxs729_maxadmin.cpp Test of 'maxadmin' user add/delete
 * - try to call Maxadmin as normal user
 * - try to call Maxadmin as 'root' user
 * - execute 'enable account'
 * - try to call Maxadmin using this enable user
 * - 'disable accout'
 * - try to enable non-existing user with very long name
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

const char * only_root = "Enabled Linux accounts (secure)    : root\n";
const char * user_added = "The Linux user %s has successfully been enabled.\n";
const char * user_removed = "The Linux user %s has successfully been disabled.\n";
const char * remove_last_admin = "Cannot remove the last admin account";
const char * root_added = "User root has been successfully added.\n";
const char * user_and_root = "Enabled Linux accounts (secure)    : %s\n";
const char * user_only = "Enabled Linux accounts (secure)    : root, %s\n";

void add_remove_maxadmin_user(TestConnections* Test)
{
    char str[1024];

    Test->tprintf("enable account %s to maxadmin:\n", Test->maxscale_access_user);
    char * st3 = Test->ssh_maxscale_output(true, "maxadmin enable account %s", Test->maxscale_access_user);
    Test->tprintf("Result: %s\n", st3);
    sprintf(str, user_added, Test->maxscale_access_user);
    if (strstr(st3, str) == NULL)
    {
        Test->add_result(1, "There is no proper '%s' message\n", str);
    }
    else
    {
        Test->tprintf("OK\n");
    }

    Test->tprintf("trying maxadmin without 'root':\n");
    char * st4 = Test->ssh_maxscale_output(false, "maxadmin show users");
    Test->tprintf("Result: %s\n", st4);
    sprintf(str, user_only, Test->maxscale_access_user);
    if (strstr(st4, str) == NULL)
    {
        Test->add_result(1, "There is no proper '%s' message\n", str);
    }
    else
    {
        Test->tprintf("OK\n");
    }

    Test->tprintf("trying maxadmin with 'root':\n");
    int st5 = Test->ssh_maxscale(true, "maxadmin show users");
    if (st5 != 0)
    {
        Test->add_result(1, "User added and access to MaxAdmin as 'root' is not possible\n");
    }
    else
    {
        Test->tprintf("OK\n");
    }

    Test->tprintf("trying maxadmin without 'root'\n");
    char * st7 = Test->ssh_maxscale_output(false, "maxadmin show users");
    Test->tprintf("Result: %s\n", st7);
    sprintf(str, user_and_root, Test->maxscale_access_user);
    if (strstr(st7, str) == NULL)
    {
        Test->add_result(1, "There is no proper '%s' message\n", str);
    }
    else
    {
        Test->tprintf("OK\n");
    }

    Test->tprintf("creating readonly user");
    Test->ssh_maxscale(false, "maxadmin add readonly-user test test");

    Test->tprintf("trying to remove user '%s'\n", Test->maxscale_access_user);
    char * st8 = Test->ssh_maxscale_output(false, "maxadmin disable account %s", Test->maxscale_access_user);

    if (strstr(st8, remove_last_admin))
    {
        Test->add_result(1, "Wrong output of disable command: %s", st8);
    }
    else
    {
        Test->tprintf("OK\n");
    }

    Test->tprintf("Trying with removed user '%s'\n", Test->maxscale_access_user);
    int st9 = Test->ssh_maxscale(false, "maxadmin show users");
    if (st9 == 0)
    {
        Test->add_result(1, "User '%s' should be removed", Test->maxscale_access_user);
    }
    else
    {
        Test->tprintf("OK\n");
    }
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(600);

    Test->ssh_maxscale(true, "rm -rf /var/lib/maxscale/passwd");
    Test->ssh_maxscale(true, "rm -rf /var/lib/maxscale/maxadmin-users");
    Test->restart_maxscale();

    Test->tprintf("trying maxadmin without 'root'\n");
    int st1 = Test->ssh_maxscale(false, "maxadmin show users");
    Test->tprintf("exit code is: %d\n", st1);
    if (st1 == 0)
    {
        Test->add_result(1, "Access to MaxAdmin is possible without 'root' priveleges\n");
    }

    Test->tprintf("trying maxadmin with 'root'\n");
    char * st2 = Test->ssh_maxscale_output(true, "maxadmin show users");
    Test->tprintf("Result: \n %s\n", st2);
    if (strstr(st2, only_root) == NULL)
    {
        Test->add_result(1, "Wrong list of MaxAdmin users\n");
    }

    add_remove_maxadmin_user(Test);

    Test->tprintf("trying long wierd user\n");
    char * st10 = Test->ssh_maxscale_output(true,
                                            "maxadmin enable account yygrgtrпрекури6н33имн756ККККЕН:УИГГГГ*?:*:*fj34oru34h275g23457g2v90590+u764gv56837fbv62381§SDFERGtrg45ergfergergefewfergt456ty");
    /*Test->tprintf("Result: %s\n", st10);
    if (strstr(st10, "has been successfully added") == NULL)
    {
        Test->add_result(1, "Wrong list of MaxAdmin users\n");
    }*/

    Test->check_maxscale_alive();
    Test->ssh_maxscale(true, "rm -rf /var/lib/maxscale/passwd");
    Test->ssh_maxscale(true, "rm -rf /var/lib/maxscale/maxadmin-users");

    int rval = Test->global_result;
    delete Test;
    return rval;
}
