/**
 * MXS-2037: Wildcards not working with source in NamedServerFilter
 *
 * https://jira.mariadb.org/browse/MXS-2037
 *
 * This test only tests that ip addresses with wildcards are accepted by
 * NamedServerFilter. The actual matching functionality is not tested
 * because the client IPs can change with the different test environments
 * and that would make it complicated to check if the matching is correct.
 */


#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.set_timeout(10);
    test.maxscales->connect_maxscale(0);
    test.add_result(execute_query(test.maxscales->conn_rwsplit[0], "select 1"), "Can't connect to backend");
    test.maxscales->close_maxscale_connections(0);
    return test.global_result;
}
