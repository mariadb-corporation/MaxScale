/**
 * @file maxscale_process_user.cpp bug143 maxscale_process_user check if Maxscale priocess is running as 'maxscale'
 *
 */


#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->set_timeout(50);
    char * user = Test->ssh_maxscale_output((char *) "ps aux | grep \"\\/usr\\/bin\\/maxscale \" | cut -f 1 -d \" \" | tr -d \"\\n\" | tr -d \"\\r\"", FALSE);

    Test->add_result(strcmp(user, "maxscale"), "MaxScale process running as '%s' instead of 'maxscale'\n", user);

    Test->copy_all_logs(); return(Test->global_result);
}
