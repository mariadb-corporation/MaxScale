/**
 * @file maxscale_process_user.cpp bug143 maxscale_process_user check if Maxscale priocess is running as 'maxscale'
 *
 */



#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->set_timeout(50);
    char *user = Test->ssh_maxscale_output(false, "ps -FC maxscale|tail -n 1|cut -f 1 -d \" \"");
    char *nl = user ? strchr(user, '\n') : NULL;

    if (nl)
    {
        *nl = '\0';
    }

    Test->tprintf("MaxScale is running as '%s'", user);
    Test->add_result(strcmp(user, "maxscale"), "MaxScale process running as '%s' instead of 'maxscale'\n", user);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
