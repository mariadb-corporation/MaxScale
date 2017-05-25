/**
 * @file bad_pres.cpp check that Maxscale prints warning if persistpoolmax=-1 for all backends (bug MXS-576)
 *
 * - Maxscale.cnf contains persistpoolmax=-1 for all servers
 * - check log warning about it
 */



#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    Test->connect_maxscale();
    Test->check_log_err((char *) "warning -1", true);
    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}

