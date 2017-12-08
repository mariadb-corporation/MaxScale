/**
 * @file bug359.cpp bug359 regression case (router_options in readwritesplit causes errors in error log)
 *
 * - Maxscale.cnf contains RWSplit router definition with router_option=slave.
 * - error is expected in the log. Maxscale should not start.
 */


/*
Massimiliano 2013-11-22 09:45:13 UTC
Setting router_options=slave in readwritesplit causes:

in the error log

2013 11/22 10:35:43  Error : Couldn't find suitable Master from 3 candidates.
2013 11/22 10:35:43  Error : Failed to create router client session. Freeing allocated resources.


If no options are allowed here, it could be better to log this and/or unload the module


This is something could happen doing copy paste from readconnrouter as an example
Comment 1 Mark Riddoch 2014-02-05 11:35:57 UTC
I makes no sense for the read/write splitter to look at the slave and master router options.

Vilho Raatikka 2014-05-22 07:02:50 UTC
Added check for router option 'synced' which accepts only that, and warns the user of other unsupported options ('master'|'slave' for example). If router option is specified for read write split router, only a node in 'joined' state will be accepted as eligible backend candidate.

*/



#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    Test->check_log_err(0, (char *) "Unsupported router option \"slave\"", true);
    Test->check_log_err(0, (char *) "Failed to start all MaxScale services. Exiting", true);
    Test->check_log_err(0, (char *) "Couldn't find suitable Master", false);
    //Test->check_maxscale_alive(0);
    Test->check_maxscale_processes(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
