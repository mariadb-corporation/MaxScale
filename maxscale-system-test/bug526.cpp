/**
 * @file bug526.cpp regression case for bug 526 ( " Wrong module name crashes maxscale on connect" )
 *
 * - Maxscale.cnf with "filters=QLA|testfilter" for RWSplit router service, 'testfilter' is not defined.
 * - check error log for proper error messages and checks if ReadConn services are alive
 */

/*
Hartmut Holzgraefe 2014-09-08 13:08:46 UTC
I mistyped a module name (for a filter in this case)

  [testfilter]
  type=filter
  module=foobar

There were no warnings about this on startup at all, but at the first time trying to connect to a service this filter was used in maxscale crashed with a segmentation fault after writing the following error log entries:

  2014 09/08 15:00:53   Error : Unable to find library for module: foobar.
  2014 09/08 15:00:53   Failed to create filter 'testfilter' for service 'testrouter'.
  2014 09/08 15:00:53   Error : Failed to create Read Connection Router session.
  2014 09/08 15:00:53   Error : Invalid authentication message from backend. Error : 28000, Msg : Access denied for user 'maxuser'@'localhost' (using password: YES)
  2014 09/08 15:00:53   Error : Backend server didn't accept authentication for user denied for user 'maxuser'@'localhost' (using password: YES).

Two problems here:

1) can't check up front that my configuration is valid / without errors without connecting to each defined service

2) maxscale crashes instead of handling this situation gracefully (e.g. ignoring the misconfigured filter, or disabling the service that refers to it alltogether)
*/



#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "maxadmin_operations.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    if (Test->connect_rwsplit() == 0)
    {
        Test->add_result(1, "Filter config is broken, but service is started\n");
    }
    if (Test->connect_readconn_master() == 0)
    {
        Test->add_result(1, "Filter config is broken, but Maxscale is started\n");
    }
    if (Test->connect_readconn_slave() == 0)
    {
        Test->add_result(1, "Filter config is broken, but Maxscale is started\n");
    }

    //sleep(5);
    Test->execute_maxadmin_command((char*) "sync logs");
    Test->check_log_err((char *) "Unable to find library for module: foobar", true);
    Test->check_log_err((char *) "Failed to load filter module 'foobar'", true);
    Test->check_log_err((char *) "Failed to load filter 'testfilter' for service 'RW Split Router'", true);
    Test->check_log_err((char *)
                        "Failed to open, read or process the MaxScale configuration file /etc/maxscale.cnf. Exiting", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
