/**
 * Test auroramon monitor
 */

#include <my_config.h>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    Test->set_timeout(30);
    Test->tprintf("Executing a query through readwritesplit");
    Test->connect_rwsplit();
    Test->try_query(Test->conn_rwsplit, "show processlist");
    Test->close_rwsplit();

    Test->set_timeout(30);
    Test->tprintf("Performing cluster failover");

    // Do the failover here and wait until it is over
    sleep(10);

    Test->set_timeout(30);
    Test->tprintf("Executing a query through readwritesplit");
    Test->connect_rwsplit();
    Test->try_query(Test->conn_rwsplit, "show processlist");
    Test->close_rwsplit();

    Test->check_maxscale_alive();
    Test->copy_all_logs();
    return Test->global_result;
}

