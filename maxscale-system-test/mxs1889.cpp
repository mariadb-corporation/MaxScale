/**
 * MXS-1889: A single remaining master is valid for readconnroute configured with 'router_options=slave'
 *
 * https://jira.mariadb.org/browse/MXS-1889
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    // Give some time for things to stabilize.
    sleep(2);

    // Take down all slaves.
    test.repl->stop_node(1);
    test.repl->stop_node(2);
    test.repl->stop_node(3);

    // Give the monitor some time to detect it
    sleep(5);

    test.maxscales->connect();

    // Should succeed.
    test.try_query(test.maxscales->conn_slave[0], "SELECT 1");

    int rv = test.global_result;

    test.repl->start_node(3);
    test.repl->start_node(2);
    test.repl->start_node(1);

    return rv;
}
