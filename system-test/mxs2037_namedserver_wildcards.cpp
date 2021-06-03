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


#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.set_timeout(10);
    test.maxscale->connect_maxscale();
    test.add_result(execute_query(test.maxscale->conn_rwsplit[0], "select 1"), "Can't connect to backend");
    test.maxscale->close_maxscale_connections();
    return test.global_result;
}
