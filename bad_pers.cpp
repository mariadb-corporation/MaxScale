/**
 * @file bad_pres.cpp check that Maxscale prints warning if persistpoolmax=-1 for all backends
 *
 * - Maxscale.cnf contains persistpoolmax=-1 for all servers
 * - check log worning about it
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    Test->connect_maxscale();
    Test->check_log_err((char *) "-1", TRUE);
    Test->check_maxscale_alive();
    Test->copy_all_logs(); return(Test->global_result);
}

