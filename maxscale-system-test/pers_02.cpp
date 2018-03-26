/**
 * @file pers_02.cpp - Persistent connection tests - crash during Maxscale restart
 *
 * - Set max_connections to 20
 * - Open 75 connections to all Maxscale services
 * - Close connections
 * - Restart replication (stop all nodes and start them again, execute CHANGE MASTER TO again)
 * - Set max_connections to 2000
 * - Open 70 connections to all Maxscale services
 * - Close connections
 * - Check there is not crash during restart
 */


#include "testconnections.h"
#include "maxadmin_operations.h"


int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->set_timeout(60);
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 20;");
    Test->create_connections(0, 75, true, true, true, true);

    Test->stop_timeout();
    Test->repl->stop_nodes();
    Test->repl->start_replication();
    Test->repl->close_connections();
    Test->repl->sync_slaves();

    // Increase connection limits and wait a few seconds for the server to catch up
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 2000;");
    sleep(10);

    Test->set_timeout(60);
    Test->add_result(Test->create_connections(0, 70 , true, true, true, true),
                     "Connections creation error \n");

    Test->check_log_err(0, (char *) "fatal signal 11", false);
    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
