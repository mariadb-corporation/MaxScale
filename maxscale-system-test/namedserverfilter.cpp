/**
 * @file namedserverfilter.cpp Namedserverfilter test
 *
 * Check that a readwritesplit service with a namedserverfilter will route a
 * SELECT @@server_id to the correct server. The filter is configured with
 * `match=SELECT` which should match any SELECT query.
 */


#include <iostream>
#include "testconnections.h"

using namespace std;

int compare_server_id(TestConnections* test, char *node_id)
{
    char str[1024];
    int rval = 0;
    if (find_field(test->conn_rwsplit, "SELECT @@server_id", "@@server_id", str))
    {
        test->tprintf("Failed to query for @@server_id.\n");
        rval = 1;
    }
    else if (strcmp(node_id, str))
    {
        test->tprintf("@@server_id is %s instead of %s\n", str, node_id);
        rval = 1;
    }
    return rval;
}

int main(int argc, char **argv)
{
    TestConnections *test = new TestConnections(argc, argv);
    test->repl->connect();
    char server_id[1024];

    sprintf(server_id, "%d", test->repl->get_server_id(1));
    test->tprintf("Server ID of server2 is: %s\n", server_id);
    test->add_result(test->connect_rwsplit(), "Test failed to connect to MaxScale.\n");
    test->add_result(compare_server_id(test, server_id), "Test failed, server ID was not correct.\n");
    int rval = test->global_result;
    delete test;
    return rval;
}
