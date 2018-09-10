/**
 * @file bug495.cpp regression case for bug 495 ( Referring to a nonexisting server in servers=... doesn't
 * even raise a warning )
 *
 * - Maxscale.cnf with "servers= server1, server2,server3  ,server4,server5"
 * but 'server5' is not defined. Test checks error log for proper error message.
 * - check if Maxscale is alive
 */

/*
 *
 *  Description Hartmut Holzgraefe 2014-08-31 21:32:09 UTC
 *  Only [server1] and [server2] are defined,
 *  service [test_service] and monitor [MySQL monitor]
 *  refer to a third server "server3" in their servers=...
 *  list though ...
 *
 *  Nothing in the err or msg log hints towards a problem ...
 *  (which originally was caused by a copy/paste error that
 *  also lead to the "duplicate section name" error reported
 *  earlier ... and which would have been easy to track down
 *  if either of these problems would at least have raised a
 *  warning - took me almost an hour to track down the actual
 *  problem ... :(
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    Test->check_log_err(0, (char*) "Unable to find server", true);
    Test->check_log_err(0, (char*) "errors were encountered while processing the configuration", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
